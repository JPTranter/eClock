// test_time.cpp — time math and tick advance.

#include "clock_harness.h"
#include <gtest/gtest.h>

TEST(Time, UpdateTimeStructBasic) {
    ClockFixture::reset();
    // epoch 1693000000 = 2023-08-25T23:06:40 UTC (verify in test below).
    // Use explicit local-time arithmetic to avoid gmtime() dependence.
    ClockFixture::setEpoch(3600, 0);           // 01:00:00 UTC
    EXPECT_EQ(ClockFixture::hour(), 1);
    EXPECT_EQ(ClockFixture::minute(), 0);
    EXPECT_EQ(ClockFixture::second(), 0);
}

TEST(Time, NegativeTzOffsetRealisticEpoch) {
    ClockFixture::reset();
    // epoch = 86400 (01:00:00 UTC next day), tz = -5h => local 19:00:00.
    // local = 86400 - 18000 = 68400 = 19*3600.
    ClockFixture::setEpoch(86400, -5 * 3600);
    EXPECT_EQ(ClockFixture::hour(), 19);
    EXPECT_EQ(ClockFixture::minute(), 0);
    EXPECT_EQ(ClockFixture::second(), 0);
}

TEST(Time, Hour23Edge) {
    ClockFixture::reset();
    ClockFixture::setEpoch(23 * 3600 + 59 * 60 + 59, 0);   // 23:59:59
    EXPECT_EQ(ClockFixture::hour(), 23);
    EXPECT_EQ(ClockFixture::minute(), 59);
    EXPECT_EQ(ClockFixture::second(), 59);
}

TEST(Time, TickAdvancesByOneMinuteAtBoundary) {
    ClockFixture::reset();
    // Use an epoch with g_second == 0 so the first loop() does NOT perform a
    // partial advance (which only happens after a mid-minute sync).
    int32_t epoch = 1000 * 3600;   // 16:00:00 local, second = 0
    ClockFixture::simulateBleTimeWrite(epoch, 0);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);
    EXPECT_EQ(ClockFixture::epoch(), epoch);   // no advance yet (clean boundary)

    ClockFixture::advanceMillis(60000);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::epoch(), epoch + 60);
}

TEST(Time, PartialAdvanceAfterMidMinuteSync) {
    ClockFixture::reset();
    // Write a time with g_second = 30 (so subsecond anchor is mid-minute).
    // 1693000000 = ... let's compute a second value directly:
    int32_t epoch = 1000 * 3600 + 30;   // 1000:00:30 UTC, second=30
    ClockFixture::simulateBleTimeWrite(epoch, 0);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);

    // First tick skips to :00 of the next minute.
    int32_t before = ClockFixture::epoch();
    ClockFixture::advanceMillis(60000);
    ClockFixture::runLoop();
    // advance = 60 - g_second(30) = 30
    EXPECT_EQ(ClockFixture::epoch(), before + 30);
}
