"""Pure message-selection logic for the ePaper Clock bottom-message feature.

This module has NO Home Assistant imports so it can be unit-tested in isolation
(with pytest) without a running Home Assistant instance. It implements the
documented, settled rules:

  * A message is an entry ``{message}`` plus an optional natural ``day`` rule:
      - Default: ``{message}`` only -> lowest-precedence fallback.
      - Weekly:  ``{message, day: 'Fri'}`` / ``'Friday'`` -> every week.
      - Annual:  ``{message, day: '25 Dec'}`` -> every year.
      - One-off: ``{message, day: '14 Mar 2026'}`` -> only that date.
      `day` is case-insensitive; weekday/month names are supported.
  * Selection precedence (highest -> lowest):
      1. One-off / annual date rules matching today; LAST in the list wins.
      2. A weekday rule matching today's weekday; LAST in the list wins.
      3. A default message (no rule).
  * No match -> no message (caller writes an all-NUL payload to clear the line).
  * Overflow: the caller truncates to MAX_MESSAGE_CHARS before writing.
  * Invalid / unparseable entries are reported (not silently dropped) so the HA
    caller can log them.

See docs/research/message-feature-design.md for the full design record.
"""

from __future__ import annotations

from typing import Optional, Sequence

# The clock displays up to this many characters; anything longer is truncated
# (see the design record: 30 chars leaves a >=2-char whitespace margin to AM/PM).
MAX_MESSAGE_CHARS = 30

# Weekday name -> datetime.weekday() number (Mon=0 .. Sun=6). Accepts the full name
# or the 3-letter abbreviation, case-insensitively. Users write `day: Fri` (or
# `day: Sunday`) in configuration.yaml for a weekly message.
_WEEKDAY_ALIASES = {
    "mon": 0, "monday": 0,
    "tue": 1, "tues": 1, "tuesday": 1,
    "wed": 2, "wednesday": 2,
    "thu": 3, "thur": 3, "thurs": 3, "thursday": 3,
    "fri": 4, "friday": 4,
    "sat": 5, "saturday": 5,
    "sun": 6, "sunday": 6,
}

_MONTH_ALIASES = {
    "jan": 1, "january": 1,
    "feb": 2, "february": 2,
    "mar": 3, "march": 3,
    "apr": 4, "april": 4,
    "may": 5,
    "jun": 6, "june": 6,
    "jul": 7, "july": 7,
    "aug": 8, "august": 8,
    "sep": 9, "sept": 9, "september": 9,
    "oct": 10, "october": 10,
    "nov": 11, "november": 11,
    "dec": 12, "december": 12,
}


def _parse_weekday(value):
    """Return a weekday int (0-6) from a name or int, or None if not a weekday."""
    if isinstance(value, int) and not isinstance(value, bool):
        return value if 0 <= value <= 6 else None
    if isinstance(value, str):
        norm = value.strip().lower()
        if norm in _WEEKDAY_ALIASES:
            return _WEEKDAY_ALIASES[norm]
    return None


def _parse_month(value):
    """Return a month int (1-12) from a name or int, or None."""
    if isinstance(value, int) and not isinstance(value, bool):
        return value if 1 <= value <= 12 else None
    if isinstance(value, str):
        norm = value.strip().lower()
        if norm in _MONTH_ALIASES:
            return _MONTH_ALIASES[norm]
    return None


def _parse_day(value):
    """Parse a single ``day`` config value into (month, day, year, weekday).

    Accepts the natural forms (case-insensitive):
      * ``"Fri"`` / ``"Friday"``        -> (None, None, None, weekday=4)  [weekly]
      * ``"23 Jun"``                    -> (6, 23, None, None)            [annual]
      * ``"23 Jun 2026"``               -> (6, 23, 2026, None)            [one-off]
      * ``2026``                        -> invalid (returned as None)

    Returns a tuple ``(month, day, year, weekday)`` where unused fields are None.
    If the value cannot be parsed as a date OR a weekday, returns ``(None,)*4``.
    """
    if isinstance(value, int) and not isinstance(value, bool):
        # A bare integer day needs a month to be a date; without one it's invalid.
        return (None, None, None, None)

    if not isinstance(value, str):
        return (None, None, None, None)

    parts = value.strip().split()
    if not parts:
        return (None, None, None, None)

    # Weekday name alone: "Fri" / "Friday".
    if len(parts) == 1:
        wd = _parse_weekday(parts[0])
        if wd is not None:
            return (None, None, None, wd)
        return (None, None, None, None)

    # "day month" or "day month year".
    day = _parse_int(parts[0])
    month = _parse_month(parts[1])
    if day is None or month is None:
        return (None, None, None, None)
    if not 1 <= day <= 31:
        return (None, None, None, None)

    year = None
    if len(parts) >= 3:
        year = _parse_int(parts[2])
        if year is None:
            return (None, None, None, None)
    return (month, day, year, None)


def _parse_int(value):
    """Return int(value) if it is a plain integer, else None."""
    try:
        return int(str(value).strip())
    except (ValueError, TypeError):
        return None


class Message:
    """One configured message: a date-based, weekday-based, or default rule."""

    __slots__ = ("message", "month", "day", "year", "weekday", "default")

    def __init__(
        self,
        message: str,
        month: Optional[int] = None,
        day: Optional[int] = None,
        year: Optional[int] = None,
        weekday: Optional[int] = None,
        default: bool = False,
    ) -> None:
        self.message = message
        self.month = month
        self.day = day
        self.year = year
        self.weekday = weekday
        self.default = default

    @property
    def is_date_based(self) -> bool:
        """True if this is a month/day (optionally year) rule, not a weekday rule."""
        return self.month is not None and self.day is not None

    @property
    def is_weekday_based(self) -> bool:
        """True if this is a weekday rule (not a date rule and not the default)."""
        return self.weekday is not None and self.month is None and self.day is None

    @property
    def is_default(self) -> bool:
        """True if this is the catch-all default (no date and no weekday rule)."""
        return self.default

    def matches_date(self, month: int, day: int, year: int) -> bool:
        """True if this date-based message fires on the given local date."""
        if not self.is_date_based:
            return False
        if self.month != month or self.day != day:
            return False
        if self.year is not None and self.year != year:
            return False
        return True

    def matches_weekday(self, weekday: int) -> bool:
        """True if this weekday-based message fires on the given weekday."""
        if not self.is_weekday_based:
            return False
        return self.weekday == weekday


def select_message(
    messages: Sequence[Message],
    month: int,
    day: int,
    year: int,
    weekday: Optional[int] = None,
) -> Optional[str]:
    """Return the message text for the given local date+weekday, or None.

    Precedence (highest -> lowest):
      1. A date-based message that matches today; last match wins.
      2. A weekday message for today's weekday (if ``weekday`` is provided); last
         match wins.
      3. A default message (no date/weekday rule); last one in the list wins.
    """
    # 1) Date-based messages take precedence; last match wins.
    date_chosen = None
    for entry in messages:
        if entry.matches_date(month, day, year):
            date_chosen = entry.message
    if date_chosen is not None:
        return date_chosen

    # 2) Fall back to a weekday message; last match wins. Skip if no weekday given.
    if weekday is not None:
        weekday_chosen = None
        for entry in messages:
            if entry.matches_weekday(weekday):
                weekday_chosen = entry.message
        if weekday_chosen is not None:
            return weekday_chosen

    # 3) Lowest precedence: a default message (no date/weekday rule).
    default_chosen = None
    for entry in messages:
        if entry.is_default:
            default_chosen = entry.message
    return default_chosen


def truncate_message(message: str, max_chars: int = MAX_MESSAGE_CHARS) -> str:
    """Truncate to at most ``max_chars`` characters (drop the tail), never blank."""
    if message is None:
        return ""
    if len(message) <= max_chars:
        return message
    return message[:max_chars]


def encode_message(message: str, width: int = MAX_MESSAGE_CHARS) -> bytes:
    """Encode the message as a fixed-width, NUL-padded ASCII byte string.

    The firmware reads exactly ``width`` bytes and treats the first NUL as the end
    of the message (so a shorter message is safely space/NUL terminated). If
    ``message`` is None/empty, all NULs are returned (clears the line).
    """
    if not message:
        return bytes(width)
    data = message.encode("utf-8", errors="replace")[:width]
    return data.ljust(width, b"\x00")


def parse_messages(raw_entries) -> tuple[list[Message], list[str]]:
    """Parse config dicts into (:class:`Message` objects, error messages).

    Each entry has ``message`` plus an optional ``day`` rule in one of these forms
    (case-insensitive):
      * Default:  ``message`` only (no `day`) — lowest-precedence fallback.
      * Weekly:   ``day: Fri`` / ``day: Friday``.
      * Annual:   ``day: 25 Dec``.
      * One-off:  ``day: 14 Mar 2026``.

    Returns ``(messages, errors)``. ``errors`` is a list of human-readable strings
    (one per invalid entry) so the Home Assistant layer can log them; invalid entries
    are NOT silently converted into a default message.
    """
    messages: list[Message] = []
    errors: list[str] = []

    for index, entry in enumerate(raw_entries or [], start=1):
        if not isinstance(entry, dict):
            errors.append(f"messages[{index}]: not a mapping: {entry!r}")
            continue

        text = entry.get("message")
        if not isinstance(text, str) or not text.strip():
            errors.append(f"messages[{index}]: missing or empty 'message'")
            continue

        # Anything other than the natural `day` rule is no longer supported.
        unknown = set(entry.keys()) - {"message", "day"}
        if unknown:
            errors.append(
                f"messages[{index}]: unsupported key(s) {sorted(unknown)} — "
                f"use 'day' only (e.g. day: Fri, day: 25 Dec, day: 14 Mar 2026)"
            )
            continue

        day_val = entry.get("day")
        month, day, year, weekday = _parse_day(day_val)

        if weekday is not None:
            messages.append(Message(str(text).strip(), weekday=weekday))
        elif month is not None and day is not None:
            messages.append(Message(str(text).strip(), month=month, day=day, year=year))
        elif day_val is None:
            # No `day` at all -> default message.
            messages.append(Message(str(text).strip(), default=True))
        else:
            # A `day` was given but couldn't be parsed -> invalid (config error).
            errors.append(
                f"messages[{index}]: could not parse day={day_val!r} "
                f"(expected a weekday name, \"day month\", or \"day month year\")"
            )

    return messages, errors
