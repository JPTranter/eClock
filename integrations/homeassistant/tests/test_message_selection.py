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


def parse(entries):
    """parse_messages returning only the Message list (drop the errors)."""
    return parse_messages(entries)[0]


# ---------------------------------------------------------------------------
# select_message (given already-built Message objects)
# ---------------------------------------------------------------------------

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
    annual = Message("Birthday", month=5, day=10)
    one_off = Message("Birthday 2023", month=5, day=10, year=2023)
    assert select_message([annual, one_off], 5, 10, 2023) == "Birthday 2023"
    assert select_message([annual, one_off], 5, 10, 2024) == "Birthday"
    assert select_message([one_off, annual], 5, 10, 2023) == "Birthday"


def test_multiple_messages_empty_list():
    assert select_message([], 12, 25, 2023) is None


# ---------------------------------------------------------------------------
# truncation / encoding
# ---------------------------------------------------------------------------

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
    assert encode_message("") == b"\x00" * MAX_MESSAGE_CHARS
    assert encode_message(None) == b"\x00" * MAX_MESSAGE_CHARS


def test_encode_message_truncates_long():
    long = "A" * (MAX_MESSAGE_CHARS + 10)
    data = encode_message(long)
    assert len(data) == MAX_MESSAGE_CHARS
    assert data == b"A" * MAX_MESSAGE_CHARS


def test_encode_message_handles_unicode():
    data = encode_message("Héllo")
    assert len(data) == MAX_MESSAGE_CHARS
    assert data[:1] == b"H"
    assert isinstance(data, bytes) and len(data) == MAX_MESSAGE_CHARS


# ---------------------------------------------------------------------------
# parse_messages — the natural `day` rule forms
# ---------------------------------------------------------------------------

def test_annual_fires_every_year():
    msgs = parse([{"message": "Merry Christmas", "day": "25 Dec"}])
    assert len(msgs) == 1
    assert msgs[0].is_date_based and msgs[0].year is None
    assert select_message(msgs, 12, 25, 2023) == "Merry Christmas"
    assert select_message(msgs, 12, 25, 2030) == "Merry Christmas"


def test_one_off_fires_only_on_its_year():
    msgs = parse([{"message": "One-off", "day": "1 Jan 2024"}])
    assert len(msgs) == 1
    assert msgs[0].is_date_based and msgs[0].year == 2024
    assert select_message(msgs, 1, 1, 2024) == "One-off"
    assert select_message(msgs, 1, 1, 2025) is None


def test_weekday_message_shows_on_that_weekday():
    msgs = parse([{"message": "Thank God it's Friday!", "day": "Fri"}])
    assert len(msgs) == 1
    assert msgs[0].weekday == 4 and msgs[0].is_date_based is False
    assert select_message(msgs, 6, 21, 2024, weekday=4) == "Thank God it's Friday!"
    assert select_message(msgs, 6, 21, 2024, weekday=3) is None


def test_default_message_used_when_nothing_else_matches():
    msgs = parse(
        [
            {"message": "God is good"},
            {"message": "TGIF", "day": "Fri"},
            {"message": "Xmas", "day": "25 Dec"},
        ]
    )
    # Non-Friday, no date match -> default.
    assert select_message(msgs, month=3, day=15, year=2027, weekday=1) == "God is good"
    # Friday with no date match -> weekday wins.
    assert select_message(msgs, month=3, day=19, year=2027, weekday=4) == "TGIF"
    # Christmas -> date wins.
    assert select_message(msgs, month=12, day=25, year=2027, weekday=6) == "Xmas"


def test_date_overrides_weekday_message():
    msgs = parse(
        [
            {"message": "Thank God it's Friday!", "day": "Fri"},
            {"message": "A special Friday", "day": "21 Jun 2024"},
        ]
    )
    # Friday 21 Jun 2024: the one-off date wins.
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "A special Friday"
    # Friday 28 Jun 2024: no date match -> weekday fallback.
    assert select_message(msgs, month=6, day=28, year=2024, weekday=4) == "Thank God it's Friday!"


def test_last_match_wins_within_date_group():
    msgs = parse(
        [
            {"message": "Annual A", "day": "21 Jun"},
            {"message": "One-off B", "day": "21 Jun 2024"},
        ]
    )
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "One-off B"


def test_weekday_last_match_wins():
    msgs = parse([{"message": "First Friday", "day": "Fri"}, {"message": "Second Friday", "day": "Fri"}])
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "Second Friday"


def test_default_last_default_wins():
    msgs = parse([{"message": "First"}, {"message": "Second"}])
    assert select_message(msgs, month=1, day=1, year=2027, weekday=1) == "Second"


def test_default_is_lowest_precedence_over_weekday():
    msgs = parse([{"message": "Fallback"}, {"message": "Church day", "day": "Sun"}])
    assert select_message(msgs, month=1, day=3, year=2027, weekday=6) == "Church day"
    assert select_message(msgs, month=1, day=4, year=2027, weekday=0) == "Fallback"


# ---------------------------------------------------------------------------
# parse_messages — weekday/month name parsing and case-insensitivity
# ---------------------------------------------------------------------------

def test_day_accepts_all_weekday_names():
    names = {"Mon": 0, "Tue": 1, "Wed": 2, "Thu": 3, "Fri": 4, "Sat": 5, "Sun": 6}
    for name, wd in names.items():
        msgs = parse([{"message": name, "day": name}])
        assert len(msgs) == 1, name
        assert msgs[0].weekday == wd, name
        assert msgs[0].month is None, name


def test_day_weekday_case_insensitive_and_full_names():
    for raw, wd in [("fri", 4), ("FRI", 4), ("Friday", 4), ("saturday", 5), ("sun", 6), ("Tuesday", 1)]:
        msgs = parse([{"message": raw, "day": raw}])
        assert len(msgs) == 1
        assert msgs[0].weekday == wd


def test_day_date_forms_case_insensitive():
    for raw, exp in [("25 dec", (25, 12, None)), ("25 DEC", (25, 12, None)), ("14 Mar 2026", (14, 3, 2026))]:
        msgs = parse([{"message": raw, "day": raw}])
        assert (msgs[0].day, msgs[0].month, msgs[0].year) == exp, raw


# ---------------------------------------------------------------------------
# parse_messages — error reporting (returns (messages, errors))
# ---------------------------------------------------------------------------

def test_parse_returns_error_for_missing_message():
    msgs, errors = parse_messages([{"day": "Fri"}])
    assert msgs == []
    assert len(errors) == 1
    assert "message" in errors[0]


def test_parse_returns_error_for_empty_message():
    msgs, errors = parse_messages([{"message": "  ", "day": "Fri"}])
    assert msgs == []
    assert len(errors) == 1
    assert "message" in errors[0]


def test_parse_returns_error_for_unsupported_key():
    # The older explicit keys are no longer supported -> reported as an error.
    msgs, errors = parse_messages([{"message": "Xmas", "month": 12, "day": 25}])
    assert msgs == []
    assert len(errors) == 1
    assert "unsupported key" in errors[0] and "month" in errors[0]


def test_parse_returns_error_for_unparseable_day():
    for bad in ["32 Dec", "13 Foo", "25", "2026", "Funday"]:
        msgs, errors = parse_messages([{"message": "Bad", "day": bad}])
        assert msgs == [], bad
        assert len(errors) == 1, bad
        assert "parse" in errors[0], bad


def test_parse_messages_empty_or_none():
    assert parse_messages(None) == ([], [])
    assert parse_messages([]) == ([], [])


def test_parse_mixed_valid_and_invalid():
    msgs, errors = parse_messages(
        [
            {"message": "OK", "day": "Fri"},
            {"message": "", "day": "Fri"},
            {"message": "Bad", "day": "Funday"},
            {"message": "Everyday"},
        ]
    )
    assert [m.message for m in msgs] == ["OK", "Everyday"]
    assert len(errors) == 2
    assert all("message" in e or "parse" in e for e in errors)


def test_parse_weekday_numeric_field_no_longer_supported():
    # The numeric `weekday` key is an explicit key -> now reported as unsupported.
    msgs, errors = parse_messages([{"message": "TGIF", "weekday": 4}])
    assert msgs == []
    assert len(errors) == 1
    assert "unsupported key" in errors[0]
