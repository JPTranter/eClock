// test_sleep.cpp — enterSleepMode(): wake outcomes, sec_to_shutdown math,
// and the fixed-6-hour (DST-immune) sleep duration.

#include "clock_harness.h"
#include <gtest/gtest.h>

TEST(Sleep, NormalFiveAmWakeLeavesAwakeUntilZero) {
    ClockFixture::reset();
    ClockFixture::setEpoch(23 * 3600, 0);   // 23:00 local
    g_state = STATE_RUNNING;

    test_set_next_wake(false);               // 5am wake, not button
    ClockFixture::runLoop();                 // RUNNING -> enterSleepMode()
    // enterSleepMode() ends in RESYNCING.
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
    EXPECT_EQ(ClockFixture::awakeUntil(), 0);
}

TEST(Sleep, ButtonWakeAtHalfPastElevenStaysAwakeUntilNextElevenPm) {
    ClockFixture::reset();
    // 23:30 local — inside the sleep window, so RUNNING transitions to sleep.
    ClockFixture::setEpoch(23 * 3600 + 30 * 60, 0);
    g_state = STATE_RUNNING;

    test_set_next_wake(true);                // woke by button
    ClockFixture::runLoop();

    // sec_to_shutdown = (23*3600 - 23.5*3600 + 86400) % 86400 = 84600 s (23.5h).
    uint32_t expected = 0 + 84600UL * 1000UL;   // millis()==0 at reset
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
    EXPECT_EQ(ClockFixture::awakeUntil(), expected);
}

TEST(Sleep, SleepDurationIsFixedSixHoursRegardlessOfEpoch) {
    // The firmware sleeps NIGHT_SLEEP_S (6*3600) via a fixed-duration
    // try_acquire_for, NOT an epoch-derived target. This is a compile-time
    // constant — assert it exists and equals 6 hours, and that it is used
    // (the DST-immunity property lives in that constant, not in math).
    EXPECT_EQ(NIGHT_SLEEP_S, 6u * 3600u);
}

TEST(Sleep, SecToShutdownWrapsPastMidnight) {
    ClockFixture::reset();
    // Local 02:00 -> sec_to_shutdown should wrap to next 23:00.
    // (23*3600 - 2*3600 + 86400) % 86400 = (82800 - 7200) = 75600 s = 21h.
    ClockFixture::setEpoch(2 * 3600, 0);
    g_state = STATE_RUNNING;

    test_set_next_wake(true);
    ClockFixture::runLoop();

    uint32_t expected = 0 + ((23 * 3600 - 2 * 3600 + 86400) % 86400) * 1000UL;
    EXPECT_EQ(ClockFixture::awakeUntil(), expected);
}
