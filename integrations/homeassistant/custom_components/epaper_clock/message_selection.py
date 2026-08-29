"""Pure message-selection logic for the ePaper Clock bottom-message feature.

This module has NO Home Assistant imports so it can be unit-tested in isolation
(with pytest) without a running Home Assistant instance. It implements the
documented, settled rules:

  * A message is an entry that is EITHER date-based or weekday-based:
      - Date-based: ``{message, month, day, year?}``.
          * If ``year`` is present, it fires ONLY on that exact date.
          * If ``year`` is absent, it fires every year on (month, day).
      - Weekday-based: ``{message, weekday}`` where weekday is 0=Monday .. 6=Sunday
        (matching ``datetime.weekday()``). Fires every week on that weekday.
  * Selection precedence:
      1. Date-based messages that match today win over weekday messages.
      2. If none match by date, a weekday message matching today's weekday is used.
      3. Within each group, the LAST matching entry in the list wins (so a specific
         one-off can override an annual, and a later weekday entry overrides an
         earlier one).
      4. No match -> no message (caller writes an all-NUL payload to clear the line).
  * Overflow: the caller truncates to MAX_MESSAGE_CHARS before writing.

See docs/research/message-feature-design.md for the full design record.
"""

from __future__ import annotations

from typing import Optional, Sequence

# The clock displays up to this many characters; anything longer is truncated
# (see the design record: 30 chars leaves a >=2-char whitespace margin to AM/PM).
MAX_MESSAGE_CHARS = 30


class Message:
    """One configured message: a date-based (month/day/year?) or weekday-based rule."""

    __slots__ = ("message", "month", "day", "year", "weekday")

    def __init__(
        self,
        message: str,
        month: Optional[int] = None,
        day: Optional[int] = None,
        year: Optional[int] = None,
        weekday: Optional[int] = None,
    ) -> None:
        self.message = message
        self.month = month
        self.day = day
        self.year = year
        self.weekday = weekday

    @property
    def is_date_based(self) -> bool:
        """True if this is a month/day (optionally year) rule, not a weekday rule."""
        return self.month is not None and self.day is not None

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
        if self.is_date_based:
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

    Precedence: a date-based message that matches today wins over a weekday
    message; within each group the LAST matching entry in the list wins.

    ``weekday`` may be omitted (None) to skip the weekday fallback entirely —
    useful when only date rules are configured.
    """
    # 1) Date-based messages take precedence; last match wins.
    date_chosen = None
    for entry in messages:
        if entry.matches_date(month, day, year):
            date_chosen = entry.message
    if date_chosen is not None:
        return date_chosen

    # 2) Fall back to a weekday message; last match wins. Skip if no weekday given.
    if weekday is None:
        return None
    weekday_chosen = None
    for entry in messages:
        if entry.matches_weekday(weekday):
            weekday_chosen = entry.message
    return weekday_chosen


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


def parse_messages(raw_entries) -> list[Message]:
    """Parse a list of config dicts into :class:`Message` objects.

    Each entry is ``{message, month, day, year?}`` (date-based) OR
    ``{message, weekday}`` (weekday-based). ``year`` is OPTIONAL for date rules
    (absent = annual). ``weekday`` is 0=Monday..6=Sunday. Invalid entries (missing
    message, or no usable date/weekday) are skipped; the caller may log them.
    """
    messages: list[Message] = []
    for entry in raw_entries or []:
        text = entry.get("message")
        if not text:
            continue

        month = entry.get("month")
        day = entry.get("day")
        weekday = entry.get("weekday")

        if month is not None and day is not None:
            messages.append(
                Message(
                    str(text),
                    month=int(month),
                    day=int(day),
                    year=int(entry["year"]) if entry.get("year") is not None else None,
                )
            )
        elif weekday is not None:
            messages.append(Message(str(text), weekday=int(weekday)))
        # else: no usable rule -> skip.
    return messages
