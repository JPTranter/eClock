// test_clock_logic.cpp — the pure, hardware-free clock_logic module.
//
// This test compiles clock_logic.cpp directly (no Arduino/mbed/GxEPD), so it
// runs fast and exercises the decision math in isolation. It mirrors the
// behaviour asserted end-to-end by the main.cpp harness tests (test_time.cpp,
// test_sleep.cpp, test_state_machine.cpp).

#include "clock_logic.h"
#include <gtest/gtest.h>

using namespace clock_logic;

// ---- splitLocalTime ---------------------------------------------------------

TEST(ClockLogic, SplitsEpochIntoLocalHourMinuteSecond) {
    TimeParts t = splitLocalTime(3600, 0);   // 01:00:00 UTC, tz 0
    EXPECT_EQ(t.hour, 1);
    EXPECT_EQ(t.minute, 0);
    EXPECT_EQ(t.second, 0);
}

TEST(ClockLogic, AppliesNegativeTimezoneOffset) {
    // 86400 = 01:00:00 UTC next day; tz -5h -> local 19:00:00.
    TimeParts t = splitLocalTime(86400, -5 * 3600);
    EXPECT_EQ(t.hour, 19);
    EXPECT_EQ(t.minute, 0);
    EXPECT_EQ(t.second, 0);
}

TEST(ClockLogic, Hour23Edge) {
    TimeParts t = splitLocalTime(23 * 3600 + 59 * 60 + 59, 0);
    EXPECT_EQ(t.hour, 23);
    EXPECT_EQ(t.minute, 59);
    EXPECT_EQ(t.second, 59);
}

// ---- localSecondsOfDay ------------------------------------------------------

TEST(ClockLogic, LocalSecondsWrapsToRange) {
    // tz pushes past 24h -> wraps to [0, 86400).
    EXPECT_EQ(localSecondsOfDay(23 * 3600, 2 * 3600), 1 * 3600);
    // Negative tz before midnight -> wraps forward.
    EXPECT_EQ(localSecondsOfDay(2 * 3600, -3 * 3600), 23 * 3600);
}

// ---- isNighttime ------------------------------------------------------------

TEST(ClockLogic, NighttimeAtElevenPm) {
    EXPECT_TRUE(isNighttime(23 * 3600));
    EXPECT_TRUE(isNighttime(23 * 3600 + 30 * 60));   // 23:30
    EXPECT_TRUE(isNighttime(2 * 3600));              // 02:00 (wraps)
    EXPECT_FALSE(isNighttime(5 * 3600));             // 05:00 exactly -> day
    EXPECT_FALSE(isNighttime(12 * 3600));            // midday
}

// ---- secondsToShutdown ------------------------------------------------------

TEST(ClockLogic, SecondsToShutdownBeforeElevenPm) {
    // 16:00 -> 7h = 25200 s to 23:00.
    EXPECT_EQ(secondsToShutdown(16 * 3600), 7u * 3600u);
}

TEST(ClockLogic, SecondsToShutdownWrapsPastMidnight) {
    // 02:00 next day -> (23*3600 - 2*3600 + 86400) % 86400 = 75600 s (21h).
    EXPECT_EQ(secondsToShutdown(2 * 3600), 75600u);
}

TEST(ClockLogic, SecondsToShutdownAtExactBedtime) {
    // Exactly 23:00 -> the NEXT 23:00 is 24h away, not 0 s.
    // Returning 0 here would make g_awake_until = millis() (already expired),
    // causing the clock to immediately re-enter sleep after a button wake.
    EXPECT_EQ(secondsToShutdown(23 * 3600), 86400u);
}

// ---- battery classification -------------------------------------------------

TEST(ClockLogic, LowBatteryBoundary) {
    // kLowBatteryPct = 20; boundary inclusive.
    EXPECT_TRUE(isLowBattery(20));
    EXPECT_TRUE(isLowBattery(19));
    EXPECT_FALSE(isLowBattery(21));
    EXPECT_FALSE(isLowBattery(100));
}

TEST(ClockLogic, CriticalBatteryBoundary) {
    // kCriticalBatteryPct = 5; boundary inclusive.
    EXPECT_TRUE(isCriticalBattery(5));
    EXPECT_TRUE(isCriticalBattery(0));
    EXPECT_FALSE(isCriticalBattery(6));
}

TEST(ClockLogic, ConstantsAreStable) {
    // Guard the values the firmware and docs promise.
    EXPECT_EQ(kLowBatteryPct, 20);
    EXPECT_EQ(kCriticalBatteryPct, 5);
    EXPECT_EQ(kSleepStartHour, 23);
    EXPECT_EQ(kSleepEndHour, 5);
    EXPECT_EQ(kNightSleepSec, 6u * 3600u);
}
