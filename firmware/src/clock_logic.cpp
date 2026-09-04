#include "clock_logic.h"

namespace clock_logic {

// Split epoch + UTC offset into local hour/minute/second.
//   local = epoch + tzOffset
// A negative local time wraps via the standard % (matching the original
// updateTimeStruct() semantics, documented unchanged).
TimeParts splitLocalTime(int32_t epoch, int32_t tzOffset) {
    int32_t local = epoch + tzOffset;
    TimeParts t;
    t.hour   = (uint8_t)((local / 3600) % 24);
    t.minute = (uint8_t)((local / 60) % 60);
    t.second = (uint8_t)(local % 60);
    return t;
}

int32_t localSecondsOfDay(int32_t epoch, int32_t tzOffset) {
    int32_t local = (epoch + tzOffset) % 86400;
    if (local < 0) local += 86400;   // wrap to [0, 86400)
    return local;
}

bool isNighttime(int32_t localSec) {
    // 23:00-05:00. Handles the wrap across midnight (>= 23 or < 5).
    return (localSec / 3600 >= kSleepStartHour ||
            localSec / 3600 < kSleepEndHour);
}

uint32_t secondsToShutdown(int32_t localSec) {
    // Seconds from now to the next 23:00, wrapping past midnight.
    // At exactly 23:00 the next shutdown is 24h away, not 0s — returning 0
    // would make g_awake_until = millis() (already expired), causing the
    // clock to immediately re-enter sleep after a button wake at bedtime.
    uint32_t s = (uint32_t)((kSleepStartHour * 3600 - localSec + 86400) % 86400);
    return (s == 0) ? 86400 : s;
}

bool isLowBattery(int pct) {
    return pct <= kLowBatteryPct;
}

bool isCriticalBattery(int pct) {
    return pct <= kCriticalBatteryPct;
}

}  // namespace clock_logic
