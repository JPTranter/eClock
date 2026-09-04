// clock_display.h — ePaper screen rendering for the eClock.
//
// A hardware-free set of rendering routines that draw each clock screen onto an
// ePaper display. The functions are TEMPLATED on the display type so the same
// source compiles against both the real GxEPD2 driver (on device) and the host
// test mock (GFXcanvas1). Everything a screen needs is passed in as a
// ClockView, so this module has no dependency on main.cpp's file-scoped state,
// ArduinoBLE, the battery ADC, or the board pins.
//
// The display type must provide the Adafruit_GFX drawing API (setFont, print,
// drawBitmap, getTextBounds, setCursor, width, height, fillScreen,
// setTextColor, setPartialWindow, setFullWindow, firstPage, nextPage).
//
// main.cpp builds a ClockView from its statics and calls these functions inside
// the display page loop. The host tests can call them directly with a ClockView.

#pragma once

#include <Arduino.h>
#include <time.h>

#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "FontTitanOne.h"     // 104pt Titan One for the time digits (0-9, :)
#include "material_icons.h"      // battery / bolt / sync-status bitmaps
#include "clock_version.h"       // ECLOCK_VERSION
#include "clock_logic.h"

namespace clock_display {

// Pixel colours (SSD1680/GxEPD2 values). Defined locally so this header has no
// dependency on the GxEPD2 driver header that supplies kBlack/GxEPD_WHITE.
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

// Return the vertical centre of a bitmap's VISIBLE ink (rows that have at least
// one non-zero bit). Several icons (e.g. the battery) have blank padding rows at
// the bottom, so their bounding-box centre does NOT equal their visible-ink
// centre. Aligning by visible ink is what makes text/icons look co-centred.
inline int visibleBitmapCentreY(const uint8_t* bitmap, int w, int h) {
    int rb = (w + 7) / 8;
    int first = -1, last = -1;
    for (int y = 0; y < h; ++y) {
        bool ink = false;
        for (int b = 0; b < rb && !ink; ++b) {
            ink |= (bitmap[y * rb + b] != 0);
        }
        if (ink) {
            if (first < 0) first = y;
            last = y;
        }
    }
    if (first < 0) return h / 2;   // empty bitmap: fall back to box centre
    return (first + last) / 2;
}

// Return the FIRST column (x) of a bitmap that has visible ink. Icons have
// horizontal padding / sidebearing, so their ink left edge differs from the bitmap
// origin (column 0). Used to seat neighbouring elements snugly by ink, not box.
inline int visibleBitmapInkLeft(const uint8_t* bitmap, int w, int h) {
    int rb = (w + 7) / 8;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            int byte = bitmap[y * rb + (x >> 3)];
            if (byte & (0x80 >> (x & 7))) return x;
        }
    }
    return 0;   // empty bitmap
}

// Everything a screen needs to know about the clock, passed by value so the
// renderers are pure functions of their inputs.
struct ClockView {
    // Time (already broken down into local hour/min/sec).
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;

    // Raw epoch + tz offset (for the date line; g_epoch + g_tz_offset).
    int32_t  epoch;
    int32_t  tzOffset;

    // Last successful sync time.
    uint8_t  lastSyncHour;
    uint8_t  lastSyncMinute;

    // Sync status flags.
    bool     lastSyncFailed;
    bool     isSyncing;       // advertising / waiting for a sync
    bool     bleInitialized;  // MAC address is valid

    // MAC address to show on the syncing screen ("" if invalid). The caller
    // owns the backing string and keeps it alive for the duration of the draw.
    const char* macAddress;

    // Battery: a valid percentage, or -1 for USB power (show the bolt icon).
    int      batteryPercent;

    // Firmware version string (e.g. "v0.1.0"), shown at the bottom left of the
    // syncing screen. "" hides it.
    const char* version;

    // Bottom-row message pushed by Home Assistant (NUL-terminated, up to
    // MESSAGE_MAX_CHARS). "" (or nullptr) hides it. The caller owns the string and
    // keeps it alive for the duration of the draw.
    const char* message;
};

// Draw the "No Time!" error screen (boot failed to sync).
template <typename Display>
void drawNoTimeScreen(Display& d) {
    d.setFont(&FreeSansBold24pt7b);
    const char* errMsg = "No Time!";
    int16_t ex, ey;
    uint16_t ew, eh;
    d.getTextBounds(errMsg, 0, 0, &ex, &ey, &ew, &eh);
    d.setCursor((d.width() - ew) / 2 - ex, (d.height() + eh) / 2);
    d.print(errMsg);

    d.setFont(&FreeSans9pt7b);
    // 2px bottom margin + 5px for descenders (g/y/j drop below the baseline).
    d.setCursor(0, d.height() - 2 - 5);
    d.print(F("Press any button to sync"));
}

// Draw the "Syncing..." screen, with the MAC address once BLE is initialised.
template <typename Display>
void drawSyncingScreen(Display& d, const ClockView& v) {
    d.setFont(&FreeSansBold24pt7b);
    const char* msg = v.isSyncing ? "Syncing..." : "Re-sync..";
    int16_t mx, my;
    uint16_t mw, mh;
    d.getTextBounds(msg, 0, 0, &mx, &my, &mw, &mh);
    d.setCursor((d.width() - mw) / 2 - mx, (d.height() + mh) / 2);
    d.print(msg);

    if (v.bleInitialized && v.macAddress) {
        d.setFont(&FreeSans9pt7b);
        int16_t macx, macy;
        uint16_t macw, mach;
        d.getTextBounds(v.macAddress, 0, 0, &macx, &macy, &macw, &mach);
        d.setCursor(d.width() - macw - 2, d.height() - 2 - 4);
        d.print(v.macAddress);
    }

    // Firmware version at the bottom left (a quick way to know what's flashed).
    if (v.version && v.version[0]) {
        d.setFont(&FreeSans9pt7b);
        d.setCursor(2, d.height() - 2 - 5);
        d.print(v.version);
    }
}

// Draw the running clock face (the settled layout):
//   Top row (single right-aligned group):
//     [date]                        [sync time?][sync icon]<-2px->[%][battery icon]
//   - sync icon top edge flush at y=0, text baseline = 14.
//   Middle: big time (unchanged).
//   Bottom row: HA message left-aligned at x=2 (clamped so it never reaches AM/PM),
//   AM/PM at the bottom-right.


template <typename Display>
void drawMainTime(Display& d, const char* timeBuf) {
    // --- Big time digits, centred (unchanged) --------------------------------
    d.setFont(&font);
    d.setTextWrap(false);
    int16_t tx, ty;
    uint16_t tw, th;
    d.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
    d.setCursor((d.width() - tw) / 2 - tx, FONT_BASELINE);   // baseline from the font header
    d.print(timeBuf);
}

template <typename Display>
void drawTopStatusRow(Display& d, const ClockView& v, const char* dateBuf) {
    // --- Top row: single right-aligned sync+power group ----------------------
    const int kBaseline = 14;         // top-row text baseline (aligns with flush-top icon)
    const int kRightMargin = 2;       // right anchor margin
    d.setFont(&FreeSans9pt7b);

    // Date (top-left) at the group baseline.
    d.setCursor(2, kBaseline);
    d.print(dateBuf);

    // Choose the sync/power icons.
    uint8_t sync_w, sync_h;
    const uint8_t* sync_bmp;
    if (v.isSyncing)      { sync_bmp = icon_syncing_bitmap; sync_w = icon_syncing_w; sync_h = icon_syncing_h; }
    else if (v.lastSyncFailed) { sync_bmp = icon_failed_bitmap; sync_w = icon_failed_w; sync_h = icon_failed_h; }
    else                  { sync_bmp = icon_synced_bitmap; sync_w = icon_synced_w; sync_h = icon_synced_h; }

    bool low = clock_logic::isLowBattery(v.batteryPercent);
    const uint8_t* batt_bmp = low ? icon_battery_empty_bitmap : icon_battery_bitmap;
    uint8_t batt_w = low ? icon_battery_empty_w : icon_battery_w;
    uint8_t batt_h = low ? icon_battery_empty_h : icon_battery_h;

    // The shared horizontal centre line for the whole group. The sync/power icons are
    // drawn flush at the top (top edge y=0) and aligned to a FIXED centre line, so the
    // group is stable regardless of which sync glyph is active. (The synced/failed
    // glyphs share visible centre 8.0 when flush; ``syncing`` is a shorter glyph, so we
    // must NOT derive the line from it or the whole group jumps up.)
    const int kGroupCentreY = 8;

    // Build right-to-left: battery icon at the right edge, then %, then sync icon,
    // then (if failed) the last-sync time — all right-anchored with fixed ink gaps.
    int x = d.width() - kRightMargin - batt_w;
    int pct_ink_left = x;   // % text ink left edge (== battery left edge if no %)

    // USB (batteryPercent < 0): bolt instead of battery, no %.
    if (v.batteryPercent < 0) {
        int bolt_c = visibleBitmapCentreY(icon_bolt_bitmap, icon_bolt_w, icon_bolt_h);
        d.drawBitmap(x, kGroupCentreY - bolt_c, icon_bolt_bitmap,
                     icon_bolt_w, icon_bolt_h, kBlack);
        // Seat the sync icon against the bolt's INK left edge (the bolt has blank
        // left padding, so align by ink, not the bitmap origin).
        int bolt_ink_left = x + visibleBitmapInkLeft(icon_bolt_bitmap, icon_bolt_w, icon_bolt_h);
        pct_ink_left = bolt_ink_left;
        x -= icon_bolt_w;
    } else {
        // Align the battery's VISIBLE ink centre with the group centre line
        // (1px higher so it sits just above the text baseline of the others).
        int batt_c = visibleBitmapCentreY(batt_bmp, batt_w, batt_h);
        d.drawBitmap(x, kGroupCentreY - batt_c - 1, batt_bmp, batt_w, batt_h, kBlack);

        // % text, seat it by INK: the % glyph has left sidebearing, so getTextBounds
        // returns an ink offset (bx) relative to the box origin. Ink spans
        // [box_x+bx, box_x+bx+bw]. Place the box so the ink RIGHT edge lands kWghtGap
        // px left of the battery's left edge (x).
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", v.batteryPercent);
        int16_t bx, by;
        uint16_t bw, bh;
        d.getTextBounds(pctBuf, 0, 0, &bx, &by, &bw, &bh);
        const int kWghtGap = 3;          // ink gap % -> battery icon
        int pct_x = x - kWghtGap - (bx + bw);
        d.setCursor(pct_x, kBaseline);
        d.print(pctBuf);
        pct_ink_left = pct_x + bx;       // % ink left edge
        x = pct_x;
    }

    // Sync icon: seat it so its ink right edge is ~2px left of the % text's ink left
    // edge. The sync icon is flush at the top.
    const int kSyncGap = 2;              // ink gap sync icon -> %
    int sync_x = (pct_ink_left - kSyncGap) - sync_w;
    d.drawBitmap(sync_x, 0, sync_bmp, sync_w, sync_h, kBlack);  // icon top flush at y=0
    x = sync_x;

    // Last-sync time, only when the last sync failed (so the user sees how stale).
    if (v.lastSyncFailed) {
        char syncTime[8];
        snprintf(syncTime, sizeof(syncTime), "%02u:%02u",
                 v.lastSyncHour, v.lastSyncMinute);
        int16_t sx, sy;
        uint16_t sw, shh;
        d.getTextBounds(syncTime, 0, 0, &sx, &sy, &sw, &shh);
        d.setCursor(x - 2 - sw, kBaseline);
        d.print(syncTime);
    }
}

template <typename Display>
void drawBottomMessageRow(Display& d, const ClockView& v, const char* ampm) {
    // --- Bottom row: HA message (left-aligned) + AM/PM -----------------------
    // AM/PM at the bottom-right.
    int16_t apx, apy;
    uint16_t apw, aph;
    d.getTextBounds(ampm, 0, 0, &apx, &apy, &apw, &aph);
    int ampm_left = d.width() - apw - 2;
    d.setCursor(ampm_left, d.height() - 2 - 4);
    d.print(ampm);

    // Message left-aligned at x=2, clamped so its ink right edge stays at least a
    // small gap (>= 2 whitespace chars) left of AM/PM. If it overflows, we drop
    // trailing characters rather than let it collide.
    if (v.message && v.message[0]) {
        d.setFont(&FreeSans9pt7b);
        d.setCursor(2, d.height() - 2 - 5);
        // Determine how many characters fit before the AM/PM gap.
        int max_right = ampm_left - 4;   // >= a couple px gap before AM/PM
        int best_end = 0;
        for (const char* p = v.message; *p && *p != '\n'; ++p) {
            // measure the prefix up to and including this char
            char tmp[40];
            int n = 0;
            for (const char* q = v.message; q <= p && n < (int)sizeof(tmp) - 1; ++q) {
                tmp[n++] = *q;
            }
            tmp[n] = '\0';
            int16_t ccx, ccy;
            uint16_t ccw, cch;
            d.getTextBounds(tmp, 0, 0, &ccx, &ccy, &ccw, &cch);
            if (2 + ccw > max_right) break;
            best_end = n;
        }
        if (best_end > 0) {
            char clipped[40];
            memcpy(clipped, v.message, best_end);
            clipped[best_end] = '\0';
            d.print(clipped);
        }
    }
}

template <typename Display>
void drawRunningFace(Display& d, const ClockView& v) {
    // Convert 24h to 12h display, suppress leading zero.
    uint8_t disp_hour = v.hour % 12;
    if (disp_hour == 0) disp_hour = 12;
    const char* ampm = (v.hour < 12) ? "AM" : "PM";

    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%u:%02u", disp_hour, v.minute);

    // Date string from the local epoch.
    time_t t = v.epoch + v.tzOffset;
    struct tm* tm_info = gmtime(&t);
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", tm_info);

    drawMainTime(d, timeBuf);
    drawTopStatusRow(d, v, dateBuf);
    drawBottomMessageRow(d, v, ampm);
}

// Draw the sleeping placeholder ("Zzz"). drawClockFace() uses this for the
// defensive SLEEPING case.
template <typename Display>
void drawSleepingScreen(Display& d) {
    d.setFont(&FreeSansBold24pt7b);
    const char* msg = "Zzz";
    int16_t zx, zy;
    uint16_t zw, zh;
    d.getTextBounds(msg, 0, 0, &zx, &zy, &zw, &zh);
    d.setCursor((d.width() - zw) / 2 - zx, (d.height() + zh) / 2 - 14);
    d.print(msg);
}

// Draw the full bedtime sleep icon ("Zzz" + "Sleeping"). This is the screen
// left on the panel before the panel rail is cut overnight.
template <typename Display>
void drawSleepIcon(Display& d) {
    d.setFont(&FreeSansBold24pt7b);
    const char* msg = "Zzz";
    int16_t mx, my;
    uint16_t mw, mh;
    d.getTextBounds(msg, 0, 0, &mx, &my, &mw, &mh);
    d.setCursor((d.width() - mw) / 2 - mx, (d.height() + mh) / 2 - 14);
    d.print(msg);

    // 'Sleeping' below.
    d.setFont(&FreeSans9pt7b);
    const char* sub = "Sleeping";
    int16_t sx, sy;
    uint16_t sw, sh;
    d.getTextBounds(sub, 0, 0, &sx, &sy, &sw, &sh);
    d.setCursor((d.width() - sw) / 2 - sx, (d.height() + mh) / 2 + 14);
    d.print(sub);
}

// Draw the final low-battery screen. Drawn once when the cell is critical so the
// last image on the panel is an unambiguous "battery empty, charge me" message.
template <typename Display>
void drawLowBatteryScreen(Display& d) {
    // Empty-battery icon, centred near the top.
    int icon_x = (d.width() - icon_battery_empty_w) / 2;
    d.drawBitmap(icon_x, 16, icon_battery_empty_bitmap,
        icon_battery_empty_w, icon_battery_empty_h, kBlack);

    // "LOW BATTERY" — prominent, centred (18pt so it fits the panel width).
    d.setFont(&FreeSansBold18pt7b);
    const char* title = "LOW BATTERY";
    int16_t tx, ty;
    uint16_t tw, th;
    d.getTextBounds(title, 0, 0, &tx, &ty, &tw, &th);
    d.setCursor((d.width() - tw) / 2 - tx, (d.height() + th) / 2 - 8);
    d.print(title);

    // Instruction line.
    d.setFont(&FreeSans9pt7b);
    const char* sub = "Charge me now";
    int16_t sx, sy;
    uint16_t sw, sh;
    d.getTextBounds(sub, 0, 0, &sx, &sy, &sw, &sh);
    d.setCursor((d.width() - sw) / 2 - sx, d.height() - 2 - 5);
    d.print(sub);
}

}  // namespace clock_display
