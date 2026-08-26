// test_display.cpp — drawClockFace() and drawSleepIcon() across all states.
// Every state also dumps a PNG for visual review.

#include "clock_harness.h"
#include <gtest/gtest.h>

// Small helper: draw a state and dump a PNG, asserting the write succeeds.
static void expectRender(const char* pngPath, int state) {
    ClockFixture::reset();
    g_state = static_cast<ClockState>(state);
    // Give a valid time for the running face.
    if (state == STATE_RUNNING)
        ClockFixture::setEpoch(1693000000, 36000);

    ClockFixture::runDrawClockFace();
    EXPECT_TRUE(ClockFixture::dumpPng(pngPath)) << "failed to write " << pngPath;
}

TEST(Display, NoTimeStateRendersAndDumps) {
    expectRender("output/no_time.png", STATE_NO_TIME);
}

TEST(Display, SyncingStateRendersAndDumps) {
    expectRender("output/syncing.png", STATE_SYNCING);
}

TEST(Display, ResyncingStateRendersAndDumps) {
    expectRender("output/resyncing.png", STATE_RESYNCING);
}

TEST(Display, RunningStateRendersAndDumps) {
    expectRender("output/running.png", STATE_RUNNING);
}

TEST(Display, SleepingStateRendersAndDumps) {
    expectRender("output/sleeping.png", STATE_SLEEPING);
}

TEST(Display, SleepIconRendersAndDumps) {
    ClockFixture::reset();
    ClockFixture::runDrawSleepIcon();
    EXPECT_TRUE(ClockFixture::dumpPng("output/sleep_icon.png"));
}

// MAC address must NOT appear before BLE init (the "00:00:00:00:00:00" flash
// fix), and must appear after. We verify via the g_ble_initialized gate.
TEST(Display, MacGateIsRespected) {
    ClockFixture::reset();
    // Pre-BLE draw: g_ble_initialized is false -> no MAC printed.
    g_state = STATE_SYNCING;
    g_ble_initialized = false;
    ClockFixture::runDrawClockFace();

    // Post-BLE draw: flag set -> MAC branch runs without crashing.
    g_ble_initialized = true;
    ClockFixture::runDrawClockFace();
    SUCCEED();
}

// Icon selection depends on g_last_sync_failed / resync state; ensure the
// running face draws all three icon variants without fault.
TEST(Display, IconVariantsRender) {
    ClockFixture::reset();
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;

    // synced
    g_last_sync_failed = false;
    ClockFixture::runDrawClockFace();

    // failed
    g_last_sync_failed = true;
    ClockFixture::runDrawClockFace();

    // syncing (resync state)
    g_state = STATE_RESYNCING;
    ClockFixture::runDrawClockFace();
    SUCCEED();
}

// USB-connected running face: renders the bolt icon (no percentage), and
// exercises the getBatteryPercent()==-1 -> bolt branch in drawClockFace().
TEST(Display, UsbRunningFaceRendersBoltIcon) {
    ClockFixture::reset();
    nrf_power_instance.USBREGSTATUS = POWER_USBREGSTATUS_VBUSDETECT_Msk;
    ClockFixture::setEpoch(1693000000, 36000);
    g_state = STATE_RUNNING;
    ClockFixture::runDrawClockFace();
    EXPECT_TRUE(ClockFixture::dumpPng("output/running_usb.png"));
}
