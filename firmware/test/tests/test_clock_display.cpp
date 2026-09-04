// test_clock_display.cpp — the pure clock_display renderers, exercised directly
// with a ClockView (independent of the main.cpp harness).
//
// This compiles clock_display.h against the GxEPD2_BW mock (a GFXcanvas1). Each
// screen renderer is called inside a proper page loop (matching how main.cpp
// calls them), and the framebuffer is dumped to a PNG so a human (or a hash
// comparison) can confirm the layout. End-to-end behaviour is also covered by
// test_display.cpp via main.cpp's adapters; this test verifies the module in
// isolation.

#include "clock_display.h"
#include "GxEPD2_BW.h"
#include "png_dump.h"
#include <gtest/gtest.h>

// The mock lives in firmware/test/mocks; the real driver lives on device. Both
// satisfy the template's display type requirements.
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> testDisplay(
    GxEPD2_290_T94_V2(0, 0, 0, 0));

using namespace clock_display;

static ClockView makeView() {
    ClockView v{};
    v.hour = 7;
    v.minute = 46;
    v.second = 0;
    v.epoch = 1693000000;
    v.tzOffset = 36000;
    v.lastSyncHour = 0;
    v.lastSyncMinute = 0;
    v.lastSyncFailed = false;
    v.isSyncing = false;
    v.bleInitialized = true;
    v.macAddress = "AA:BB:CC:DD:EE:FF";
    v.batteryPercent = 100;
    v.version = ECLOCK_VERSION;
    v.message = "";
    return v;
}

// Run a renderer inside a page loop (same shape as main.cpp's drawClockFace),
// then dump the framebuffer to a PNG.
template <typename Fn>
static void renderAndDump(Fn&& fn, const char* path) {
    testDisplay.setFullWindow();
    testDisplay.firstPage();
    do {
        testDisplay.fillScreen(kWhite);
        testDisplay.setTextColor(kBlack);
        fn();
    } while (testDisplay.nextPage());
    EXPECT_TRUE(dump_png(path, testDisplay.getBuffer(), testDisplay.width(),
                         testDisplay.height()))
        << "failed to write " << path;
}

TEST(ClockDisplay, RunningFaceRenders) {
    renderAndDump([&] { drawRunningFace(testDisplay, makeView()); },
                  "output/cd_running.png");
}

TEST(ClockDisplay, SyncingScreenRendersMac) {
    ClockView v = makeView();
    v.isSyncing = true;
    renderAndDump([&] { drawSyncingScreen(testDisplay, v); },
                  "output/cd_syncing.png");
}

TEST(ClockDisplay, NoTimeScreenRenders) {
    renderAndDump([&] { drawNoTimeScreen(testDisplay); },
                  "output/cd_no_time.png");
}

TEST(ClockDisplay, SleepIconRenders) {
    renderAndDump([&] { drawSleepIcon(testDisplay); },
                  "output/cd_sleep_icon.png");
}

TEST(ClockDisplay, LowBatteryScreenRenders) {
    renderAndDump([&] { drawLowBatteryScreen(testDisplay); },
                  "output/cd_low_battery.png");
}

TEST(ClockDisplay, UsbShowsBoltInsteadOfEmptyBattery) {
    ClockView v = makeView();
    v.batteryPercent = -1;   // USB power sentinel
    renderAndDump([&] { drawRunningFace(testDisplay, v); },
                  "output/cd_running_usb.png");
}

TEST(ClockDisplay, LowBatterySwapsToEmptyIcon) {
    ClockView v = makeView();
    v.batteryPercent = 15;   // below the 20% low threshold
    renderAndDump([&] { drawRunningFace(testDisplay, v); },
                  "output/cd_running_low.png");
}

TEST(ClockDisplay, BottomMessageRenders) {
    ClockView v = makeView();
    v.message = "Hello from Home Assistant";
    renderAndDump([&] { drawRunningFace(testDisplay, v); },
                  "output/cd_running_message.png");
}

TEST(ClockDisplay, LongBottomMessageIsClamped) {
    ClockView v = makeView();
    // 40-char message: must not collide with AM/PM (clamped).
    v.message = "Happy Birthday Alexandra Maxime the Third";
    renderAndDump([&] { drawRunningFace(testDisplay, v); },
                  "output/cd_running_long_message.png");
}

// --- Sync x power state matrix (for visual review of the top-right group) -----

TEST(ClockDisplay, StateMatrix) {
    // Sync OK (full battery, no message) — the baseline running face.
    ClockView v = makeView();
    renderAndDump([&] { drawRunningFace(testDisplay, v); }, "output/cd_state_ok.png");

    // Sync OK, low battery (empty icon + low %).
    ClockView low = makeView();
    low.batteryPercent = 15;
    renderAndDump([&] { drawRunningFace(testDisplay, low); }, "output/cd_state_low.png");

    // Syncing (circular-arrows icon), full battery.
    ClockView syncing = makeView();
    syncing.isSyncing = true;
    renderAndDump([&] { drawRunningFace(testDisplay, syncing); }, "output/cd_state_syncing.png");

    // Sync FAILED (cross icon + last-sync time shown), full battery.
    ClockView failed = makeView();
    failed.lastSyncFailed = true;
    failed.lastSyncHour = 9; failed.lastSyncMinute = 41;
    renderAndDump([&] { drawRunningFace(testDisplay, failed); }, "output/cd_state_failed.png");

    // USB power (bolt icon, no %).
    ClockView usb = makeView();
    usb.batteryPercent = -1;
    renderAndDump([&] { drawRunningFace(testDisplay, usb); }, "output/cd_state_usb.png");

    // Failed + low battery combined.
    ClockView failLow = makeView();
    failLow.lastSyncFailed = true;
    failLow.lastSyncHour = 9; failLow.lastSyncMinute = 41;
    failLow.batteryPercent = 15;
    renderAndDump([&] { drawRunningFace(testDisplay, failLow); }, "output/cd_state_fail_low.png");
}
