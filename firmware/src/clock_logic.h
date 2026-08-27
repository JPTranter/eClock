// clock_logic.h — hardware-free decision logic for the eClock.
//
// This is the pure, unit-testable core of the firmware: time/date math,
// sleep-window computation, and battery classification. It has NO dependency on
// Arduino, ArduinoBLE, GxEPD2, mbed or any board pin — just <stdint.h>. It is
// compiled into the PlatformIO firmware build and, separately, into the host
// unit tests, so the interesting behaviour can be tested without the
// "include main.cpp" harness.
//
// main.cpp is the adapter + wiring layer: it owns the hardware (BLE, display,
// buttons, sleep) and calls these pure functions to make decisions.

#pragma once

#include <stdint.h>

namespace clock_logic {

// Battery level thresholds (percent). The percentage maps 3.3V (0%) to
// 4.2V (100%) — see getBatteryPercent() in main.cpp. Below LOW_BATTERY_PCT the
// clock swaps to the empty-battery glyph; at/below CRITICAL_BATTERY_PCT it draws
// the final "LOW BATTERY" screen.
constexpr int8_t kLowBatteryPct      = 20;
constexpr int8_t kCriticalBatteryPct = 5;

// Sleep window (local wall-clock hours). The clock sleeps between SLEEP_START
// (23:00) and SLEEP_END (05:00). The window is always 6 hours of wall-clock time
// so a DST transition mid-sleep cannot move the wake time.
constexpr uint8_t kSleepStartHour = 23;   // 11pm
constexpr uint8_t kSleepEndHour   = 5;    // 5am
constexpr uint32_t kNightSleepSec = 6 * 3600;   // 23:00 -> 05:00

// A broken-down local time, in the clock's own frames of reference.
struct TimeParts {
    uint8_t hour;    // 0-23
    uint8_t minute;  // 0-59
    uint8_t second;  // 0-59
};

// Split epoch + UTC offset into local hour/minute/second.
//   local = epoch + tzOffset   (seconds since 1970-01-01, local wall-clock)
TimeParts splitLocalTime(int32_t epoch, int32_t tzOffset);

// Local seconds-of-day from epoch + tzOffset, in [0, 86400).
int32_t localSecondsOfDay(int32_t epoch, int32_t tzOffset);

// Is the given local seconds-of-day inside the night sleep window (23:00-05:00)?
bool isNighttime(int32_t localSec);

// Seconds from the given local seconds-of-day to the next 23:00 (SLEEP_START).
// Wraps past midnight. Used when a button press cancels the night and keeps the
// clock awake until the next 11pm.
uint32_t secondsToShutdown(int32_t localSec);

// Battery classification on a *valid* (non-negative) percentage. Callers must
// reject the getBatteryPercent() == -1 (USB) sentinel first.
bool isLowBattery(int pct);
bool isCriticalBattery(int pct);

}  // namespace clock_logic
