#pragma once

// ---------------------------------------------------------------------------
// clock_harness.h — the single header user tests include.
//
// It (1) #includes the UNMODIFIED firmware source so every file-scoped static
// (g_state, g_epoch, drawClockFace, ...) is visible in the test translation
// unit, and (2) provides a ClockFixture that resets all of it to a known state
// and exposes the small helper surface tests need.
//
// Because each test executable is a separate binary, each gets its own copy of
// the statics — no cross-test interference, no ODR clashes.
// ---------------------------------------------------------------------------

#include "main.cpp"

// Mocks (for their test_* scripting hooks and reset of mock state).
#include "Arduino.h"
#include "mbed.h"
#include "ArduinoBLE.h"
#include "nrf52_regs.h"

// PNG dump helper (needed by ClockFixture::dumpPng below).
#include "png_dump.h"

// ---------------------------------------------------------------------------
// ClockFixture — reset + helpers.
// ---------------------------------------------------------------------------
class ClockFixture {
public:
    // Reset ALL firmware statics and mock state to a clean boot-equivalent
    // state (millis=0, STATE_SYNCING, no time, fresh display framebuffer,
    // pin log cleared, BLE defaults, analog default).
    static void reset() {
        // --- firmware statics ---
        g_state = STATE_SYNCING;
        g_epoch = 0;
        g_tz_offset = 0;
        g_last_tick_millis = 0;
        g_hour = 0;
        g_minute = 0;
        g_second = 0;
        g_last_sync_millis = 0;
        g_sync_attempt_start = 0;
        g_last_sync_hour = 0;
        g_last_sync_minute = 0;
        g_last_sync_failed = false;
        g_needs_display_update = true;
        g_last_button_press = 0;
        g_awake_until = 0;
        g_ble_initialized = false;
        g_button_pending = false;
        // Drain the button semaphore to count 0.
        while (g_button_sem.try_acquire()) {}

        // --- mock state ---
        test_set_millis(0);
        test_reset_pin_log();
        test_set_next_wake(false);
        test_set_analog_u16(0xFFFF);      // full-scale battery (max voltage)
        nrf_power_instance.USBREGSTATUS = 0;  // not on USB
        nrf_twim0_instance.ENABLE = 0;
        nrf_twim1_instance.ENABLE = 0;
        nrf_twi0_instance.ENABLE = 0;
        nrf_twi1_instance.ENABLE = 0;
        BLE.test_set_begin_result(true);
        BLE.test_set_address("AA:BB:CC:DD:EE:FF");

        // --- display: reset the framebuffer to all-white and clear ink ---
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(0, 0);
        display.setRotation(1);
    }

    // ---- state accessors (for assertions) ----
    static ClockState state() { return g_state; }
    static int32_t epoch() { return g_epoch; }
    static int32_t tzOffset() { return g_tz_offset; }
    static int hour() { return g_hour; }
    static int minute() { return g_minute; }
    static int second() { return g_second; }
    static bool needsDisplayUpdate() { return g_needs_display_update; }
    static bool bleInitialized() { return g_ble_initialized; }
    static bool lastSyncFailed() { return g_last_sync_failed; }
    static uint32_t awakeUntil() { return g_awake_until; }

    // ---- time helpers ----
    // Set the clock's notion of "now" (epoch + tz), updating the time struct
    // WITHOUT simulating a BLE write (no state transition).
    static void setEpoch(int32_t epoch, int32_t tz) {
        g_epoch = epoch;
        g_tz_offset = tz;
        updateTimeStruct();
    }

    // Simulate Home Assistant writing the 8-byte time payload over BLE.
    static void simulateBleTimeWrite(int32_t epoch, int32_t tz) {
        uint8_t buf[8];
        memcpy(buf, &epoch, 4);
        memcpy(buf + 4, &tz, 4);
        timeCharacteristic.test_simulate_remote_write(buf, 8);
    }

    // Simulate a button press: set the pending flag + release the semaphore.
    static void pressButton() {
        g_button_pending = true;
        g_button_sem.release();
    }

    // Advance the fake clock.
    static void advanceMillis(uint32_t ms) { test_add_millis(ms); }

    // Direct calls into the firmware (static functions are in scope here).
    static void runSetup() { setup(); }
    static void runLoop() { loop(); }
    static void runDrawClockFace() { drawClockFace(); }
    static void runDrawSleepIcon() { drawSleepIcon(); }
    static void runEnterSleepMode() { enterSleepMode(); }
    static int runGetBatteryPercent() { return getBatteryPercent(); }

    // Dump the current framebuffer to a PNG (296x128 grayscale).
    static bool dumpPng(const char* path) {
        return dump_png(path, display.getBuffer(), display.width(), display.height());
    }

    // Raw framebuffer access (for pixel-level assertions).
    static const uint8_t* framebuffer() { return display.getBuffer(); }
};
