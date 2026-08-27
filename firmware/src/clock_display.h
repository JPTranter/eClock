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
#include "FontChango88.h"        // 88pt Chango for the time digits (0-9, :)
#include "material_icons.h"      // battery / bolt / sync-status bitmaps
#include "clock_logic.h"

namespace clock_display {

// Pixel colours (SSD1680/GxEPD2 values). Defined locally so this header has no
// dependency on the GxEPD2 driver header that supplies kBlack/GxEPD_WHITE.
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;

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
}

// Draw the running clock face: date + battery on top, big time, sync status
// + AM/PM below. Shared by RUNNING and by SYNCING/RESYNCING with a valid time.
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

    // Date line at the top left.
    d.setFont(&FreeSans9pt7b);
    d.setCursor(2, 12);
    d.print(dateBuf);

    // Battery line at the top right.
    if (v.batteryPercent < 0) {
        // USB power: bolt icon only, no text.
        d.drawBitmap(d.width() - 2 - icon_bolt_w, 2,
            icon_bolt_bitmap, icon_bolt_w, icon_bolt_h, kBlack);
    } else {
        // Battery icon + percentage. Below the low threshold the icon swaps to
        // the empty-battery glyph so the low state is obvious at a glance.
        bool low = clock_logic::isLowBattery(v.batteryPercent);
        const uint8_t* bmp = low ? icon_battery_empty_bitmap : icon_battery_bitmap;
        uint8_t icon_w = low ? icon_battery_empty_w : icon_battery_w;
        uint8_t icon_h = low ? icon_battery_empty_h : icon_battery_h;
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", v.batteryPercent);
        d.setFont(&FreeSans9pt7b);
        int16_t bx, by;
        uint16_t bw, bh;
        d.getTextBounds(pctBuf, 0, 0, &bx, &by, &bw, &bh);
        // 6px gap between the battery icon and the percentage.
        int text_x = d.width() - 2 - icon_w - 6 - bw;
        int icon_x = d.width() - 2 - icon_w;
        d.drawBitmap(icon_x, 2, bmp, icon_w, icon_h, kBlack);
        d.setCursor(text_x, 12);
        d.print(pctBuf);
    }

    // Big time digits, centred.
    d.setFont(&font);
    d.setTextWrap(false);
    int16_t tx, ty;
    uint16_t tw, th;
    d.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
    // Top text bottom edge is ~16; bottom text top edge is ~114. Available
    // space is 98 px, centered at 65. Chango 88pt has yOff=-90, h=64, so the
    // ink centre relative to the baseline is -58, giving a baseline of 123.
    d.setCursor((d.width() - tw) / 2 - tx, 123);
    d.print(timeBuf);

    // Status icon + last sync time at the bottom left.
    uint8_t icon_w, icon_h;
    const uint8_t* icon_bmp;
    if (v.isSyncing) {
        icon_bmp = icon_syncing_bitmap;
        icon_w = icon_syncing_w;
        icon_h = icon_syncing_h;
    } else if (v.lastSyncFailed) {
        icon_bmp = icon_failed_bitmap;
        icon_w = icon_failed_w;
        icon_h = icon_failed_h;
    } else {
        icon_bmp = icon_synced_bitmap;
        icon_w = icon_synced_w;
        icon_h = icon_synced_h;
    }
    int icon_y = d.height() - 2 - 10 - icon_h / 2;   // centred, 2px bottom margin
    d.drawBitmap(2, icon_y, icon_bmp, icon_w, icon_h, kBlack);

    // Last successful sync time to the right of the icon.
    char syncTime[8];
    snprintf(syncTime, sizeof(syncTime), "%02u:%02u",
             v.lastSyncHour, v.lastSyncMinute);
    d.setFont(&FreeSans9pt7b);
    d.setCursor(2 + icon_w + 2, d.height() - 2 - 5);
    d.print(syncTime);

    // AM/PM at the bottom right.
    int16_t apx, apy;
    uint16_t apw, aph;
    d.getTextBounds(ampm, 0, 0, &apx, &apy, &apw, &aph);
    d.setCursor(d.width() - apw - 2, d.height() - 2 - 4);
    d.print(ampm);
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
