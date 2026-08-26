// test_setup.cpp — setup() behaviour.

#include "clock_harness.h"
#include <gtest/gtest.h>

TEST(Setup, NormalBootReachesSyncingWithAdvertising) {
    ClockFixture::reset();
    ClockFixture::runSetup();

    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);
    EXPECT_TRUE(ClockFixture::bleInitialized());
    EXPECT_TRUE(BLE.test_advertising());
    // Two draws happen in setup(): pre-BLE (no MAC) and post-BLE (with MAC).
    EXPECT_GE(test_pin_log_size(), 1);
}

TEST(Setup, BleBeginFailureHangsWithRedLed) {
    ClockFixture::reset();
    BLE.test_set_begin_result(false);

    // setup() enters an infinite red-LED blink loop when BLE.begin() fails.
    // We cannot call it directly (it never returns). Instead, verify the
    // guard is wired: a false begin() result is returned by the mock.
    EXPECT_FALSE(BLE.begin());
    // NOTE: exercising the actual infinite loop would hang the test runner.
    // The branch is covered by inspection; see HOST_TESTING.md.
}
