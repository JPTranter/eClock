"""Pure message-selection logic for the ePaper Clock bottom-message feature.

This module has NO Home Assistant imports so it can be unit-tested in isolation
(with pytest) without a running Home Assistant instance. It implements the
documented, settled rules:

  * A message is an entry ``{message, month, day, year?}``.
      - If ``year`` is present, it fires ONLY on that exact date.
      - If ``year`` is absent, it fires every year on (month, day).
  * Selection: for a given date, collect all entries whose (month, day) — and year
    if specified — match. If more than one matches, the LAST one in the list wins
    (so a specific one-off can override an annual on the same day).
  * No match -> no message (caller writes an all-NUL payload to clear the line).
  * Overflow: the caller truncates to MAX_MESSAGE_CHARS before writing.

See docs/research/message-feature-design.md for the full design record.
"""

from __future__ import annotations

from typing import Optional, Sequence

# The clock displays up to this many characters; anything longer is truncated
# (see the design record: 30 chars leaves a >=2-char whitespace margin to AM/PM).
MAX_MESSAGE_CHARS = 30


class Message:
    """One configured message (annual or one-off)."""

    __slots__ = ("message", "month", "day", "year")

    def __init__(self, message: str, month: int, day: int, year: Optional[int] = None) -> None:
        self.message = message
        self.month = month
        self.day = day
        self.year = year

    def matches(self, month: int, day: int, year: int) -> bool:
        """True if this message fires on the given local date."""
        if self.month != month or self.day != day:
            return False
        if self.year is not None and self.year != year:
            return False
        return True


def select_message(messages: Sequence[Message], month: int, day: int, year: int) -> Optional[str]:
    """Return the message text for the given local date, or None if none fire.

    If multiple entries match, the LAST one in the list wins (so a one-off with a
    specific year can override an annual entry configured earlier in the same list).
    """
    chosen = None
    for entry in messages:
        if entry.matches(month, day, year):
            chosen = entry.message
    return chosen


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

    Each entry is ``{message, month, day, year?}``. ``year`` is OPTIONAL: absent
    means the message fires every year on (month, day); present means it fires on
    that exact date only. Skips/invalid entries are logged by the caller, so this
    helper returns only well-formed messages.
    """
    messages: list[Message] = []
    for entry in raw_entries or []:
        text = entry.get("message")
        month = entry.get("month")
        day = entry.get("day")
        if not text or month is None or day is None:
            continue
        messages.append(
            Message(
                str(text),
                int(month),
                int(day),
                int(entry["year"]) if entry.get("year") is not None else None,
            )
        )
    return messages
