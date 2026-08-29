// test_state_machine.cpp — the clock's state transitions in loop().

#include "clock_harness.h"
#include <gtest/gtest.h>

// Helper: advance millis past the debounce window and press a button.
static void debouncedButtonPress() {
    ClockFixture::advanceMillis(BUTTON_DEBOUNCE_MS + 1);
    ClockFixture::pressButton();
}

TEST(State, BleTimeWriteTransitionsToRunning) {
    ClockFixture::reset();
    ClockFixture::simulateBleTimeWrite(1693000000, 36000);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);
    EXPECT_FALSE(ClockFixture::lastSyncFailed());
}

TEST(State, WrongLengthBleWriteIsIgnored) {
    ClockFixture::reset();
    // A 4-byte write is malformed; loop() must ignore it (valueLength != 8).
    uint8_t bad[4] = {0, 0, 0, 0};
    timeCharacteristic.test_simulate_remote_write(bad, 4);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);   // unchanged
    EXPECT_EQ(ClockFixture::epoch(), 0);
}

TEST(State, SyncingTimeoutWithNoEpochGoesNoTime) {
    ClockFixture::reset();
    ClockFixture::runSetup();               // enters SYNCING, starts attempt
    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);

    ClockFixture::advanceMillis(SYNC_TIMEOUT);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_NO_TIME);
}

TEST(State, ResyncingTimeoutWithEpochGoesRunningFailed) {
    ClockFixture::reset();
    ClockFixture::setEpoch(1693000000, 36000);   // has valid time
    g_state = STATE_RESYNCING;
    g_sync_attempt_start = millis();

    ClockFixture::advanceMillis(SYNC_TIMEOUT);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);
    EXPECT_TRUE(ClockFixture::lastSyncFailed());
}

TEST(State, ButtonPressTriggersResyncFromRunning) {
    ClockFixture::reset();
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;

    debouncedButtonPress();
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
    EXPECT_TRUE(BLE.test_advertising());
}

TEST(State, ButtonPressIgnoredWhileAlreadySyncing) {
    ClockFixture::reset();
    g_state = STATE_SYNCING;

    debouncedButtonPress();
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);   // unchanged
}

TEST(State, RunningBedtimeEntersSleepThenResyncs) {
    ClockFixture::reset();
    // Local time 23:00 (SLEEP_START_HOUR) -> sleep window.
    // local = epoch + tz; choose epoch so local_sec = 23*3600.
    int32_t epoch = 23 * 3600;      // 23:00:00 UTC
    ClockFixture::setEpoch(epoch, 0);
    g_state = STATE_RUNNING;

    // Default mock wake = false (5am wake), so enterSleepMode() runs to
    // completion and lands in RESYNCING.
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
}

TEST(State, RunningResyncIntervalTriggersResync) {
    ClockFixture::reset();
    ClockFixture::setEpoch(1000 * 3600, 0);   // 10:00 local (not bedtime)
    g_state = STATE_RUNNING;
    g_last_sync_millis = millis();

    ClockFixture::advanceMillis(SYNC_INTERVAL);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
}

TEST(State, AnyButtonPressedClearsFlag) {
    ClockFixture::reset();
    ClockFixture::pressButton();               // sets pending + releases sem
    EXPECT_TRUE(anyButtonPressed());           // consumes flag
    EXPECT_FALSE(anyButtonPressed());          // now clear
}

TEST(State, ButtonIsrDebounces) {
    ClockFixture::reset();
    // First call at millis 0: within debounce -> no pending set.
    button_isr();
    EXPECT_FALSE(g_button_pending);

    // After debounce window: pending set + semaphore released.
    ClockFixture::advanceMillis(BUTTON_DEBOUNCE_MS + 1);
    button_isr();
    EXPECT_TRUE(g_button_pending);
}

// The loop() STATE_SLEEPING case is defensive (enterSleepMode() is blocking),
// but it must still handle a wake event that occurs outside the sleep block.
TEST(State, SleepingStateInLoopTransitionsToResync) {
    ClockFixture::reset();
    g_state = STATE_SLEEPING;
    g_awake_until = 0;   // (now - g_awake_until) >= 0 -> treat as awake

    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
}

// The loop() STATE_NO_TIME case is a no-op default: the clock stays in
// NO_TIME until a button press triggers a manual re-sync.
TEST(State, NoTimeStateInLoopIsStable) {
    ClockFixture::reset();
    g_state = STATE_NO_TIME;

    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_NO_TIME);
}

// BUG REGRESSION TEST: a failed re-sync must NOT immediately re-enter RESYNCING.
//
// Scenario: the clock had a valid time (g_epoch != 0) and last synced at T.
// One hour later it enters RESYNCING, but Home Assistant does not respond, so
// the attempt times out after SYNC_TIMEOUT and returns to RUNNING with
// g_last_sync_failed = true. It must then back off a FULL hour before trying
// again — NOT immediately re-enter RESYNCING.
//
// Before the fix, g_last_sync_millis was only updated on a *successful* sync,
// so after a failure `now - g_last_sync_millis >= SYNC_INTERVAL` stayed true and
// the clock re-entered RESYNCING on the very next loop() iteration — a tight
// retry loop that kept the radio on and drained the battery (observed on hardware
// as the sync icon re-appearing every ~minute). See LESSONS_LEARNT.md §30.
TEST(State, FailedResyncBacksOffFullHourInsteadOfImmediateRetry) {
    ClockFixture::reset();

    // 1. Give the clock a valid time at time T (local 10:00, not bedtime).
    int32_t epoch = 1000 * 3600;         // 10:00 local
    ClockFixture::setEpoch(epoch, 0);
    g_state = STATE_RUNNING;
    // Simulate that a sync succeeded at the current millis (so the hourly gate
    // is exactly one SYNC_INTERVAL away).
    ClockFixture::simulateBleTimeWrite(epoch, 0);
    ClockFixture::runLoop();             // processes the write -> RUNNING
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);

    // 2. Advance past the hourly interval -> the clock starts a re-sync.
    ClockFixture::advanceMillis(SYNC_INTERVAL);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);

    // 3. Home Assistant is silent: let the attempt time out.
    ClockFixture::advanceMillis(SYNC_TIMEOUT);
    ClockFixture::runLoop();
    // It must go back to RUNNING (it has a time) and flag the failure.
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);
    EXPECT_TRUE(ClockFixture::lastSyncFailed());

    // 4. THE BUG: immediately after the failed attempt, the clock must NOT
    //    re-enter RESYNCING. It should back off a full hour.
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING)
        << "A failed re-sync must back off a full hour, not immediately retry "
           "(which keeps the radio on and drains the battery).";

    // 5. It must still be RUNNING (not re-syncing) even a little later, until a
    //    full SYNC_INTERVAL has elapsed since the *failed* attempt.
    ClockFixture::advanceMillis(SYNC_INTERVAL / 2);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RUNNING);

    // 6. Only after a full hour of backoff should it try again.
    ClockFixture::advanceMillis(SYNC_INTERVAL / 2);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);

    // 7. A button press during the backoff must still force a manual re-sync.
    ClockFixture::reset();
    g_state = STATE_RUNNING;
    g_last_sync_failed = true;           // in the backoff window
    // No need for a full hour: a press should override the backoff.
    debouncedButtonPress();
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_RESYNCING);
}

// A failed boot sync (no time ever) still goes to the error screen. That path
// is separate from the re-sync backoff and must be unaffected.
TEST(State, FailedReorderBootSyncStillGoesNoTime) {
    ClockFixture::reset();
    ClockFixture::runSetup();            // SYNCING, no time yet
    EXPECT_EQ(ClockFixture::state(), STATE_SYNCING);

    ClockFixture::advanceMillis(SYNC_TIMEOUT);
    ClockFixture::runLoop();
    EXPECT_EQ(ClockFixture::state(), STATE_NO_TIME);
}
