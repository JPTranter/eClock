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
    # Messages without a text are skipped; a text-only entry becomes a DEFAULT.
    msgs = parse_messages(
        [
            {"message": "OK", "month": 5, "day": 10},
            {"message": "", "month": 5, "day": 10},        # empty text -> skipped
            {"month": 5, "day": 10},                        # no message -> skipped
            {"message": "Everyday"},                        # no rule -> default
        ]
    )
    assert len(msgs) == 2
    assert msgs[0].message == "OK" and msgs[0].year is None
    assert msgs[1].is_default is True


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


# ---- weekday as a NAME in config (day: Fri) ---------------------------------

def test_day_accepts_weekday_name():
    # The user-friendly config: day: Fri (no month) -> weekday-based rule.
    msgs = parse_messages([{"message": "Thank God it's Friday!", "day": "Fri"}])
    assert len(msgs) == 1
    assert msgs[0].is_date_based is False
    assert msgs[0].weekday == 4  # Friday
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "Thank God it's Friday!"
    assert select_message(msgs, month=6, day=21, year=2024, weekday=5) is None


def test_day_accepts_all_weekday_names():
    names = {
        "Mon": 0, "Tue": 1, "Wed": 2, "Thu": 3, "Fri": 4, "Sat": 5, "Sun": 6,
    }
    for name, wd in names.items():
        msgs = parse_messages([{"message": name, "day": name}])
        assert len(msgs) == 1, name
        assert msgs[0].weekday == wd, name
        assert msgs[0].month is None, name


def test_day_weekday_name_is_case_insensitive():
    for name, wd in [("fri", 4), ("FRI", 4), ("Fri", 4), ("saturday", 5), ("sun", 6)]:
        msgs = parse_messages([{"message": name, "day": name}])
        assert len(msgs) == 1
        assert msgs[0].weekday == wd


def test_day_numeric_with_month_is_still_date_based():
    # A numeric day WITH a month remains a date-based rule (unaffected).
    msgs = parse_messages([{"message": "Merry Christmas", "month": 12, "day": 25}])
    assert msgs[0].is_date_based is True
    assert msgs[0].month == 12 and msgs[0].day == 25 and msgs[0].year is None


def test_invalid_weekday_name_is_skipped():
    # A day that's neither a weekday name nor a usable rule (no month) -> skipped.
    msgs = parse_messages([{"message": "Bad", "day": "Funday"}])
    assert msgs == []


# ---- natural day:string forms ----------------------------------------------
# day: "Mon"/"Monday" (weekly) | day: "23 Jun" (annual) | day: "23 Jun 2026" (one-off)

def test_day_string_forms():
    # 1) weekday name only -> weekly
    m1 = parse_messages([{"message": "TGIF", "day": "Fri"}])[0]
    assert m1.weekday == 4 and m1.is_date_based is False

    # 2) "day month" -> annual (fires every year)
    m2 = parse_messages([{"message": "Xmas", "day": "25 Dec"}])[0]
    assert m2.is_date_based is True
    assert (m2.day, m2.month, m2.year) == (25, 12, None)

    # 3) "day month year" -> one-off specific date
    m3 = parse_messages([{"message": "Wedding", "day": "14 Mar 2026"}])[0]
    assert m3.is_date_based is True
    assert (m3.day, m3.month, m3.year) == (14, 3, 2026)


def test_day_string_forms_are_case_insensitive():
    variants = [
        ("fri", 4, None, None, None),
        ("Friday", 4, None, None, None),
        ("25 dec", None, 25, 12, None),
        ("25 DEC", None, 25, 12, None),
        ("14 Mar 2026", None, 14, 3, 2026),
    ]
    for raw, wd, d, mo, yr in variants:
        m = parse_messages([{"message": raw, "day": raw}])[0]
        assert m.weekday == wd, raw
        if wd is None:
            assert (m.day, m.month, m.year) == (d, mo, yr), raw


def test_day_string_forms_parse_into_selectable():
    msgs = parse_messages(
        [
            {"message": "TGIF", "day": "Fri"},
            {"message": "Xmas", "day": "25 Dec"},
            {"message": "Wedding", "day": "14 Mar 2026"},
        ]
    )
    # Weekly fires on a matching Friday (no date override).
    assert select_message(msgs, month=6, day=21, year=2024, weekday=4) == "TGIF"
    # Annual fires on its month/day any year.
    assert select_message(msgs, month=12, day=25, year=2025, weekday=4) == "Xmas"
    # One-off fires only on its exact date.
    assert select_message(msgs, month=3, day=14, year=2026, weekday=6) == "Wedding"
    # 14 Mar 2027 is a Friday; with no date match the TGIF weekday message shows.
    assert select_message(msgs, month=3, day=14, year=2027, weekday=4) == "TGIF"
    # On a non-Friday with no date match, nothing shows.
    assert select_message(msgs, month=3, day=15, year=2027, weekday=1) is None
    # The more specific one-off (Wedding, 14 Mar 2026) wins over an annual on that date.
    specific = parse_messages(
        [
            {"message": "Annual Mar", "day": "14 Mar"},
            {"message": "Wedding", "day": "14 Mar 2026"},
        ]
    )
    assert select_message(specific, month=3, day=14, year=2026, weekday=4) == "Wedding"


def test_day_string_invalid_is_skipped():
    for bad in ["32 Dec", "13 Foo", "25", "2026"]:
        msgs = parse_messages([{"message": "Bad", "day": bad}])
        assert msgs == [], bad


# ---- default message (no day rule) -----------------------------------------
# A plain `message` with no day/weekday is the LOWEST-precedence fallback: shown
# when no date-based and no weekday-based message applies.

def test_default_message_is_used_only_when_nothing_else_matches():
    msgs = parse_messages(
        [
            {"message": "God is good"},
            {"message": "TGIF", "day": "Fri"},
            {"message": "Xmas", "day": "25 Dec"},
        ]
    )
    # A non-Friday with no date match -> default.
    assert select_message(msgs, month=3, day=15, year=2027, weekday=1) == "God is good"
    # A Friday with no date match -> weekday wins over default.
    assert select_message(msgs, month=3, day=19, year=2027, weekday=4) == "TGIF"
    # Christmas -> date wins over weekday and default.
    assert select_message(msgs, month=12, day=25, year=2027, weekday=6) == "Xmas"


def test_default_message_last_default_wins():
    msgs = parse_messages([{"message": "First"}, {"message": "Second"}])
    assert select_message(msgs, month=1, day=1, year=2027, weekday=1) == "Second"


def test_default_message_is_lowest_precedence_over_weekday():
    msgs = parse_messages(
        [
            {"message": "Fallback"},
            {"message": "Church day", "day": "Sun"},
        ]
    )
    # Sunday -> weekday wins.
    assert select_message(msgs, month=1, day=3, year=2027, weekday=6) == "Church day"
    # Monday -> default.
    assert select_message(msgs, month=1, day=4, year=2027, weekday=0) == "Fallback"


def test_default_message_only_no_rules():
    msgs = parse_messages([{"message": "Always shown"}])
    assert len(msgs) == 1
    # With a weekday provided but nothing matching, default shows.
    assert select_message(msgs, month=1, day=1, year=2027, weekday=3) == "Always shown"
    # Even when select_message is called without a weekday (date-only path), default shows.
    assert select_message(msgs, month=1, day=1, year=2027) == "Always shown"
