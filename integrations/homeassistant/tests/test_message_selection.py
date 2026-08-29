"""Unit tests for the ePaper Clock message-selection logic.

These test the PURE logic (no Home Assistant needed) so they can run with plain
pytest (the component directory is added to sys.path by tests/conftest.py). Run:

    python -m pytest integrations/homeassistant/custom_components/epaper_clock/tests/ -v
"""

from message_selection import (
    MAX_MESSAGE_CHARS,
    Message,
    encode_message,
    parse_messages,
    select_message,
    truncate_message,
)


def test_annual_message_fires_every_year():
    msg = Message("Merry Christmas", month=12, day=25)
    assert select_message([msg], 12, 25, 2023) == "Merry Christmas"
    assert select_message([msg], 12, 25, 2024) == "Merry Christmas"


def test_one_off_fires_only_on_its_year():
    msg = Message("Happy New Year 2024", month=1, day=1, year=2024)
    assert select_message([msg], 1, 1, 2024) == "Happy New Year 2024"
    assert select_message([msg], 1, 1, 2025) is None


def test_no_match_returns_none():
    msg = Message("Halloween", month=10, day=31)
    assert select_message([msg], 12, 25, 2023) is None


def test_last_match_wins():
    # A specific one-off (year=2023) configured after an annual on the same day.
    annual = Message("Birthday", month=5, day=10)
    one_off = Message("Birthday 2023", month=5, day=10, year=2023)
    # Last in list wins -> the one-off overrides on 2023; annual shows other years.
    assert select_message([annual, one_off], 5, 10, 2023) == "Birthday 2023"
    assert select_message([annual, one_off], 5, 10, 2024) == "Birthday"
    # If the one-off is listed first, the annual (later) wins on 2023.
    assert select_message([one_off, annual], 5, 10, 2023) == "Birthday"


def test_multiple_messages_empty_list():
    assert select_message([], 12, 25, 2023) is None


def test_truncate_to_max():
    long = "x" * (MAX_MESSAGE_CHARS + 5)
    assert len(truncate_message(long)) == MAX_MESSAGE_CHARS
    assert truncate_message(long) == "x" * MAX_MESSAGE_CHARS


def test_truncate_short_untouched():
    assert truncate_message("Hello") == "Hello"


def test_truncate_none_returns_blank():
    assert truncate_message(None) == ""


def test_encode_message_pads_to_width():
    data = encode_message("Hi")
    assert len(data) == MAX_MESSAGE_CHARS
    assert data[:2] == b"Hi"
    assert data[2:] == b"\x00" * (MAX_MESSAGE_CHARS - 2)


def test_encode_message_empty_is_all_nul():
    data = encode_message("")
    assert data == b"\x00" * MAX_MESSAGE_CHARS
    assert encode_message(None) == b"\x00" * MAX_MESSAGE_CHARS


def test_encode_message_truncates_long():
    long = "A" * (MAX_MESSAGE_CHARS + 10)
    data = encode_message(long)
    assert len(data) == MAX_MESSAGE_CHARS
    assert data == b"A" * MAX_MESSAGE_CHARS


def test_encode_message_handles_unicode():
    # Non-ASCII is replaced (safe for the ASCII-only firmware display).
    data = encode_message("Héllo")
    assert len(data) == MAX_MESSAGE_CHARS
    assert data[:1] == b"H"
    # The remaining bytes are NUL-padded; we only require no crash + fixed width.
    assert isinstance(data, bytes) and len(data) == MAX_MESSAGE_CHARS


def test_annual_message_does_not_require_year():
    # An annual event (e.g. Christmas) config has NO 'year' key; matches every year.
    msgs = parse_messages([{"message": "Merry Christmas", "month": 12, "day": 25}])
    assert len(msgs) == 1
    assert msgs[0].year is None
    assert select_message(msgs, 12, 25, 2023) == "Merry Christmas"
    assert select_message(msgs, 12, 25, 2030) == "Merry Christmas"


def test_parse_messages_with_year():
    msgs = parse_messages(
        [{"message": "One-off", "month": 1, "day": 1, "year": 2024}]
    )
    assert len(msgs) == 1
    assert msgs[0].year == 2024
    assert select_message(msgs, 1, 1, 2024) == "One-off"
    assert select_message(msgs, 1, 1, 2025) is None


def test_parse_messages_skips_invalid():
    # Missing message / month / day -> skipped; valid ones kept.
    msgs = parse_messages(
        [
            {"message": "OK", "month": 5, "day": 10},
            {"message": "", "month": 5, "day": 10},        # empty text
            {"month": 5, "day": 10},                        # no message
            {"message": "No date"},                         # no month/day
        ]
    )
    assert len(msgs) == 1
    assert msgs[0].message == "OK"
    assert msgs[0].year is None


def test_parse_messages_empty_or_none():
    assert parse_messages(None) == []
    assert parse_messages([]) == []


# ---- weekday messages -------------------------------------------------------

def test_weekday_message_shows_on_that_weekday():
    # weekday=4 -> Friday (datetime.weekday(): Mon=0 .. Sun=6).
    msgs = parse_messages([{"message": "Thank God it's Friday!", "weekday": 4}])
    assert len(msgs) == 1
    assert msgs[0].weekday == 4
    assert select_message(msgs, 6, 21, 2024, weekday=4) == "Thank God it's Friday!"
    # Does not show on other weekdays.
    assert select_message(msgs, 6, 21, 2024, weekday=3) is None


def test_date_message_overrides_weekday_message():
    # A date-based message (annual or one-off) on today beats a weekday message.
    msgs = parse_messages(
        [
            {"message": "Thank God it's Friday!", "weekday": 4},
            {"message": "A special Friday", "month": 6, "day": 21, "year": 2024},
        ]
    )
    # Friday 21 Jun 2024: date message overrides the weekday message.
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "A special Friday"
    # Friday 28 Jun 2024: no date message -> weekday message is the fallback.
    assert select_message(msgs, month=6, day=28, year=2024, weekday=4) == "Thank God it's Friday!"


def test_weekday_message_is_fallback():
    msgs = parse_messages(
        [
            {"message": "Thank God it's Friday!", "weekday": 4},
            {"message": "Merry Christmas", "month": 12, "day": 25},
        ]
    )
    # Christmas 25 Dec 2024 (a Wednesday) -> date message wins over nothing.
    assert select_message(msgs, month=12, day=25, year=2024, weekday=3) == "Merry Christmas"
    # 20 Dec 2024 (a Friday) is not Christmas -> weekday message shows.
    assert select_message(msgs, month=12, day=20, year=2024, weekday=4) == "Thank God it's Friday!"


def test_weekday_last_match_wins():
    msgs = parse_messages(
        [
            {"message": "First Friday", "weekday": 4},
            {"message": "Second Friday", "weekday": 4},
        ]
    )
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "Second Friday"


def test_weekday_mixed_with_date_last_wins_within_group():
    # Among date-based messages, last wins; weekday only used if no date match.
    msgs = parse_messages(
        [
            {"message": "Thank God it's Friday!", "weekday": 4},
            {"message": "Annual A", "month": 6, "day": 21},
            {"message": "One-off B", "month": 6, "day": 21, "year": 2024},
        ]
    )
    # 21 Jun 2024: date messages match; the LAST (One-off B) wins over Annual A.
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "One-off B"
    # 28 Jun 2024: no date match -> weekday.
    assert select_message(msgs, month=6, day=28, year=2024, weekday=4) == "Thank God it's Friday!"
