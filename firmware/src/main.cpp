#include <Arduino.h>
#include <time.h>
#if defined(ECLOCK_CORE_MBED)
#include <mbed.h>
#endif

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"
#include "material_icons.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "FontChango82.h"  // 82pt Chango for time digits (0-9, :)

GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Current Time Service BLE UUID
BLEService timeService("1805");

// Current Time Characteristic UUID 0x2A2B.
// HA component writes 8 bytes: int32 epoch, int32 tz_offset (little-endian)
BLECharacteristic timeCharacteristic("2A2B",
    BLEWrite | BLEWriteWithoutResponse | BLENotify | BLERead, 8);

// ==== State machine ============================================================
enum ClockState {
    STATE_SYNCING,       // Boot: advertising, waiting for first time sync
    STATE_RUNNING,       // Normal: have valid time, ticking
    STATE_RESYNCING,     // Periodic re-sync: advertising for an update
    STATE_NO_TIME,        // Boot sync timed out: no time available (error)
    STATE_SLEEPING        // Display off between 11pm and 5am
};

static ClockState g_state = STATE_SYNCING;

// ==== Time state ===============================================================
static int32_t  g_epoch = 0;            // seconds since 1970-01-01
static int32_t  g_tz_offset = 0;        // UTC offset in seconds
static uint32_t g_last_tick_millis = 0; // millis() of the most recent time advance

// Time components for display
static uint8_t g_hour = 0;
static uint8_t g_minute = 0;
static uint8_t g_second = 0;

// Sync timing
static uint32_t g_last_sync_millis = 0;          // millis() when time was last written
static uint32_t g_sync_attempt_start = 0;        // millis() when current attempt began
static const uint32_t SYNC_INTERVAL = 60 * 60 * 1000UL;   // 1 hour between re-syncs
static const uint32_t SYNC_TIMEOUT  = 30 * 1000UL;         // 30 seconds max per attempt

static uint8_t g_last_sync_hour = 0;
static uint8_t g_last_sync_minute = 0;
static bool g_last_sync_failed = false;

// Display state
static bool g_needs_display_update = true;  // always draw on first loop

// Button debounce
static uint32_t g_last_button_press = 0;
static const uint32_t BUTTON_DEBOUNCE_MS = 300;

// Semaphore for interrupt-driven button wake. The ISR releases this on a
// falling edge; the power management code blocks on it via try_acquire_for(),
// so the CPU can sleep for up to a full minute and still respond instantly
// to button presses.
// A separate volatile flag tracks whether a press occurred even when the
// semaphore is consumed by the wake mechanism.
static rtos::Semaphore g_button_sem(0, 1);
static volatile bool g_button_pending = false;

// Sleep mode: display off between 11pm and 5am. Button wake sets a cooldown
// so the clock stays awake for 15 minutes before re-entering sleep.
static uint32_t g_awake_until = 0;
static const uint32_t WAKE_COOLDOWN = 900000;   // 15 minutes
static const uint8_t  SLEEP_START_HOUR = 23;    // 11pm
static const uint8_t  SLEEP_END_HOUR   = 5;     // 5am

// ==== Buttons ==================================================================
// EN05 has three user buttons on D1, D2, D9: active LOW with INPUT_PULLUP.
// GPIOTE interrupts on the falling edge wake the CPU from WFE sleep, so we
// never need to poll digitalRead. The ISR hardware-debounces (ignores bounces
// within BUTTON_DEBOUNCE_MS of each other).

static void button_isr() {
    static uint32_t last_isr_ms = 0;
    uint32_t now = millis();
    if (now - last_isr_ms > BUTTON_DEBOUNCE_MS) {
        last_isr_ms = now;
        g_button_pending = true;   // persist for detection
        g_button_sem.release();    // wake blocked try_acquire_for()
    }
}

static void initButtons() {
    pinMode(BUTTON_1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_1), button_isr, FALLING);
    pinMode(BUTTON_2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_2), button_isr, FALLING);
    pinMode(BUTTON_3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_3), button_isr, FALLING);
}

static bool anyButtonPressed() {
    // Read and clear the flag. The semaphore is consumed separately
    // (by try_acquire_for for wake, or by this call for detection),
    // but the flag persists across semaphore consumption.
    // No memory barrier needed on single-core Cortex-M4.
    bool pressed = g_button_pending;
    g_button_pending = false;
    if (pressed) {
        // Consume the semaphore if it wasn't already consumed by a wake.
        g_button_sem.try_acquire();
    }
    return pressed;
}

// ==== Time helpers =============================================================
static void updateTimeStruct() {
    int32_t local_time = g_epoch + g_tz_offset;
    g_hour = (local_time / 3600) % 24;
    g_minute = (local_time / 60) % 60;
    g_second = local_time % 60;
}

static void startSyncAttempt() {
    // Restart BLE advertising so HA can discover and write to us
    // On mbed, re-apply parameters before advertising to ensure the stack actually broadcasts
    BLE.setLocalName("ePaper Clock");
    BLE.setDeviceName("ePaper Clock");
    BLE.setAdvertisedService(timeService);
    
    // Add a changing manufacturer data byte so Home Assistant doesn't deduplicate
    // the advertisement if we restart advertising within its cache window.
    static uint8_t adv_counter = 0;
    adv_counter++;
    // Use an arbitrary Company ID (e.g., 0xFFFF for testing) + our counter
    static uint8_t mfg_data[3];
    mfg_data[0] = 0xFF;
    mfg_data[1] = 0xFF;
    mfg_data[2] = adv_counter;
    BLE.setManufacturerData(mfg_data, sizeof(mfg_data));
    
    BLE.advertise();
    g_sync_attempt_start = millis();
}

// ==== Display helpers ==========================================================

static int getBatteryPercent() {
    // If USB is connected, VBUS overrides the battery voltage — the divider
    // reading would show ~5V and the percentage calculation would be junk.
    // Skip the ADC and just show "USB" on the display instead.
    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) {
        return -1;  // caller renders "USB"
    }
    
    // Enable battery divider
    pinMode(14, OUTPUT);
    digitalWrite(14, LOW);
    delay(10); // stabilize
    
    // analogRead(32) crashes in Seeed's mbed core due to an out-of-bounds array access.
    // Instead, we directly instantiate an mbed AnalogIn on P0_31.
    int raw = 0;
    {
        mbed::AnalogIn vbat_adc(P0_31);
        raw = vbat_adc.read_u16() >> 4; // 16-bit down to 12-bit
    }
    
    // Disable divider
    digitalWrite(14, HIGH);
    
    // Let the mbed core properly destroy the AnalogIn object and recreate the DigitalOut
    // object so GxEPD2's digitalWrite(32) works again. This avoids crashing the mbed OS
    // by manually clobbering the SAADC hardware registers beneath it!
    pinMode(32, OUTPUT);
    
    // Calculate battery percentage (assuming 12-bit, Vref=3.6V, 1/2 divider)
    float vbat = (raw * 7.2f) / 4096.0f;
    
    // Map 3.3V (empty) to 4.2V (full)
    int pct = (int)((vbat - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    
    return pct;
}

static void drawClockFace() {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        switch (g_state) {
            // ---------------------------------------------------------------
            // NO_TIME: error state — boot failed to sync within 10 seconds
            // ---------------------------------------------------------------
            case STATE_NO_TIME: {
                display.setFont(&FreeSansBold24pt7b);
                const char* errMsg = "No Time!";
                int16_t ex, ey;
                uint16_t ew, eh;
                display.getTextBounds(errMsg, 0, 0, &ex, &ey, &ew, &eh);
                display.setCursor((display.width() - ew) / 2 - ex,
                                  (display.height() + eh) / 2);
                display.print(errMsg);

                display.setFont(&FreeSans9pt7b);
                display.setCursor(0, display.height());
                display.print(F("Press any button to sync"));
                break;
            }

            // ---------------------------------------------------------------
            // SYNCING / RESYNCING: waiting for HA to write the time
            // ---------------------------------------------------------------
            case STATE_SYNCING:
            case STATE_RESYNCING: {
                if (g_epoch > 0) {
                    goto draw_running_face;
                }
                
                display.setFont(&FreeSansBold24pt7b);
                const char* msg = (g_state == STATE_SYNCING)
                                  ? "Syncing..."
                                  : "Re-sync..";
                int16_t mx, my;
                uint16_t mw, mh;
                display.getTextBounds(msg, 0, 0, &mx, &my, &mw, &mh);
                display.setCursor((display.width() - mw) / 2 - mx,
                                  (display.height() + mh) / 2);
                display.print(msg);
                
                break;
            }

            // ---------------------------------------------------------------
            // RUNNING: big single-line time, status below
            // ---------------------------------------------------------------
            case STATE_RUNNING:
            draw_running_face: {
                // Convert 24h to 12h display, suppress leading zero
                uint8_t disp_hour = g_hour % 12;
                if (disp_hour == 0) disp_hour = 12;
                const char* ampm = (g_hour < 12) ? "AM" : "PM";

                char timeBuf[8];
                snprintf(timeBuf, sizeof(timeBuf), "%u:%02u", disp_hour, g_minute);

                // Calculate date string
                time_t t = g_epoch + g_tz_offset;
                struct tm *tm_info = gmtime(&t);
                char dateBuf[32];
                strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", tm_info);

                // Date line at the top left
                display.setFont(&FreeSans9pt7b);
                display.setCursor(2, 12);
                display.print(dateBuf);
                
                // Battery line at the top right
                int batPct = getBatteryPercent();
                if (batPct < 0) {
                    // USB power: bolt icon only, no text
                    display.drawBitmap(display.width() - 2 - icon_bolt_w, 2,
                        icon_bolt_bitmap, icon_bolt_w, icon_bolt_h, GxEPD_BLACK);
                } else {
                    // Battery: battery icon + percentage
                    char pctBuf[8];
                    snprintf(pctBuf, sizeof(pctBuf), "%d%%", batPct);
                    display.setFont(&FreeSans9pt7b);
                    int16_t bx, by;
                    uint16_t bw, bh;
                    display.getTextBounds(pctBuf, 0, 0, &bx, &by, &bw, &bh);
                    int text_x = display.width() - 2 - icon_battery_w - 2 - bw;
                    int icon_x = display.width() - 2 - icon_battery_w;
                    display.drawBitmap(icon_x, 2,
                        icon_battery_bitmap, icon_battery_w, icon_battery_h, GxEPD_BLACK);
                    display.setCursor(text_x, 12);
                    display.print(pctBuf);
                }

                // Use the big time font
                display.setFont(&font);
                display.setTextWrap(false);

                // Centre the time string horizontally
                int16_t tx, ty;
                uint16_t tw, th;
                display.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
                
                // Top text bottom edge is ~16. Bottom text top edge is ~114.
                // Available space is 114 - 16 = 98 pixels. Center is 16 + 49 = 65.
                // Chango 82pt has typical yOff=-84, h=62. Center of ink relative
                // to baseline is -84 + 31 = -53. Baseline = 65 - (-53) = 118.
                display.setCursor((display.width() - tw) / 2 - tx, 118);
                display.print(timeBuf);

                // Status icon + last sync time at the bottom left
                uint8_t icon_w, icon_h;
                const uint8_t* icon_bmp;
                if (g_state == STATE_RESYNCING || g_state == STATE_SYNCING) {
                    icon_bmp = icon_syncing_bitmap;
                    icon_w = icon_syncing_w;
                    icon_h = icon_syncing_h;
                } else if (g_last_sync_failed) {
                    icon_bmp = icon_failed_bitmap;
                    icon_w = icon_failed_w;
                    icon_h = icon_failed_h;
                } else {
                    icon_bmp = icon_synced_bitmap;
                    icon_w = icon_synced_w;
                    icon_h = icon_synced_h;
                }
                
                // Icon vertically centred with text, both shifted up to fit 2px bottom margin
                // text_mid ≈ 116 (midpoint of h-2 - icon_h/2 zone), icon_y = text_mid - icon_h/2
                int icon_y = display.height() - 2 - 10 - icon_h / 2;  // ≈106 for 19px icon
                display.drawBitmap(2, icon_y, icon_bmp, icon_w, icon_h, GxEPD_BLACK);

                // Last successful sync time to the right of the icon
                char syncTime[8];
                snprintf(syncTime, sizeof(syncTime), "%02u:%02u", g_last_sync_hour, g_last_sync_minute);
                display.setFont(&FreeSans9pt7b);
                display.setCursor(2 + icon_w + 2, display.height() - 2 - 5);  // baseline shifted up to centre with icon
                display.print(syncTime);

                // AM/PM at the bottom right
                int16_t apx, apy;
                uint16_t apw, aph;
                display.getTextBounds(ampm, 0, 0, &apx, &apy, &apw, &aph);
                display.setCursor(display.width() - apw - 2, display.height() - 2 - 4);
                display.print(ampm);
                break;
            }

            // ---------------------------------------------------------------
            // SLEEPING: should not be reached (drawSleepIcon is called
            // separately), but handle gracefully as a blank screen.
            // ---------------------------------------------------------------
            case STATE_SLEEPING: {
                display.setFont(&FreeSansBold24pt7b);
                const char* msg = "Zz";
                int16_t zx, zy;
                uint16_t zw, zh;
                display.getTextBounds(msg, 0, 0, &zx, &zy, &zw, &zh);
                display.setCursor((display.width() - zw) / 2 - zx,
                                  (display.height() + zh) / 2 - 14);
                display.print(msg);
                break;
            }
        }
    } while (display.nextPage());
}

static void drawSleepIcon() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Big 'Zz' centred
        display.setFont(&FreeSansBold24pt7b);
        const char* msg = "Zz";
        int16_t mx, my;
        uint16_t mw, mh;
        display.getTextBounds(msg, 0, 0, &mx, &my, &mw, &mh);
        display.setCursor((display.width() - mw) / 2 - mx,
                          (display.height() + mh) / 2 - 14);
        display.print(msg);

        // 'Sleeping' below
        display.setFont(&FreeSans9pt7b);
        const char* sub = "Sleeping";
        int16_t sx, sy;
        uint16_t sw, sh;
        display.getTextBounds(sub, 0, 0, &sx, &sy, &sw, &sh);
        display.setCursor((display.width() - sw) / 2 - sx,
                          (display.height() + mh) / 2 + 14);
        display.print(sub);
    } while (display.nextPage());
}

static void enterSleepMode() {
    // Draw the sleep icon before cutting panel power
    drawSleepIcon();
    g_needs_display_update = false;

    // Stop BLE radio
    BLE.stopAdvertise();

    // Power off the ePaper panel (D6 MOSFET gate LOW)
    digitalWrite(EPD_POWER, LOW);

    // Calculate seconds until 5am local time
    int32_t local_sec = (g_epoch + g_tz_offset) % 86400;
    if (local_sec < 0) local_sec += 86400;
    uint32_t sec_to_5am = (SLEEP_END_HOUR * 3600 - local_sec + 86400) % 86400;

    // Block in WFE sleep until a button press or wake-up time
    uint32_t sleep_ms = (sec_to_5am > 0 && sec_to_5am <= 12 * 3600)
                        ? sec_to_5am * 1000 : 0;
    bool woke_by_button = false;
    if (sleep_ms > 0) {
        woke_by_button = g_button_sem.try_acquire_for(
                            std::chrono::milliseconds(sleep_ms));
    }

    // Re-power the ePaper panel
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);
    display.init(115200, true, 2, false);
    display.setRotation(1);
    
    // Mbed OS SPI initialization strips the pull-up from the MISO pin (D9).
    // We must forcefully re-apply it so BUTTON_3 doesn't float and cause an IRQ storm.
    pinMode(BUTTON_3, INPUT_PULLUP);

    // If button woke us, set cooldown so we don't immediately re-sleep
    if (woke_by_button) {
        g_awake_until = millis() + WAKE_COOLDOWN;
        anyButtonPressed();   // consume any latent button flag
    } else {
        g_awake_until = 0;    // normal 5am wake, allow bedtime check
    }

    // Transition to re-sync to get fresh time from HA
    g_state = STATE_RESYNCING;
    g_needs_display_update = true;
    startSyncAttempt();
}

// ==== Tick interval (60 seconds) ==============================================
static const uint32_t TICK_INTERVAL = 60000;  // Advance time once per minute

// ==== Setup ====================================================================
void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    initButtons();

    // Power the ePaper panel
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    // Release mbed TWIM pins if they conflict (legacy, not needed on this board)
#if defined(ECLOCK_CORE_MBED) && defined(NRF_TWIM0)
    eclock_release_twim_pins();
#endif

    display.init(115200, true, 2, false);
    display.setRotation(1);  // landscape: 296 wide × 128 tall
    
    // Mbed OS SPI initialization strips the pull-up from the MISO pin (D9).
    // We must forcefully re-apply it so BUTTON_3 doesn't float and cause an IRQ storm.
    pinMode(BUTTON_3, INPUT_PULLUP);

    // Draw the "Syncing..." screen before BLE init so the user sees feedback
    // before the BLE stack (which can take ~1s) finishes starting
    drawClockFace();

    if (!BLE.begin()) {
        while (1) {
            digitalWrite(LED_RED, LOW);
            delay(100);
            digitalWrite(LED_RED, HIGH);
            delay(100);
        }
    }

    BLE.setLocalName("ePaper Clock");
    BLE.setDeviceName("ePaper Clock");
    BLE.setAdvertisedService(timeService);
    timeService.addCharacteristic(timeCharacteristic);
    BLE.addService(timeService);

    uint8_t init_val[8] = {0};
    timeCharacteristic.writeValue(init_val, sizeof(init_val));

    // Begin first sync attempt — the state is already STATE_SYNCING
    startSyncAttempt();

    g_last_tick_millis = millis();
}

// ==== Main loop ================================================================
void loop() {
    BLE.poll();
    uint32_t now = millis();
    // ---------------------------------------------------------------
    // 1. BLE characteristic write: Home Assistant sent us the time
    // ---------------------------------------------------------------
    if (timeCharacteristic.written()) {
        if (timeCharacteristic.valueLength() == 8) {
            const uint8_t* val = timeCharacteristic.value();
            memcpy(&g_epoch, val, 4);
            memcpy(&g_tz_offset, val + 4, 4);

            // Update state regardless of what it was
            g_state = STATE_RUNNING;
            g_last_sync_millis = now;
            updateTimeStruct();
            g_needs_display_update = true;
            g_last_sync_hour = g_hour;
            g_last_sync_minute = g_minute;
            g_last_sync_failed = false;

            // Align the tick timer to the current second. We set the baseline
            // such that the first tick happens at the next minute boundary
            // (:00 seconds). This ensures the clock snaps to the correct minute
            // on the very first display update.
            uint32_t subsecond_ms = g_second * 1000;
            g_last_tick_millis = now - subsecond_ms;  // "anchor" at :00 of current minute

            // Shut down radio to save power now that we have the time
            BLE.stopAdvertise();

            // Flash green LED briefly as feedback
            digitalWrite(LED_GREEN, LOW);
            delay(10);
            digitalWrite(LED_GREEN, HIGH);
        }
    }

    // ---------------------------------------------------------------
    // 2. Button press: manual re-sync on demand
    // ---------------------------------------------------------------
    if (anyButtonPressed() && (now - g_last_button_press > BUTTON_DEBOUNCE_MS)) {
        g_last_button_press = now;

        // Only trigger if we aren't already in the middle of a sync attempt
        if (g_state != STATE_SYNCING && g_state != STATE_RESYNCING) {
            g_state = STATE_RESYNCING;
            g_needs_display_update = true;
            startSyncAttempt();
        }
    }

    // ---------------------------------------------------------------
    // 3. State machine transitions
    // ---------------------------------------------------------------
    switch (g_state) {

        case STATE_SYNCING:
        case STATE_RESYNCING: {
            // Timed out waiting for a sync?
            if (millis() - g_sync_attempt_start >= SYNC_TIMEOUT) {
                BLE.stopAdvertise();

                if (g_epoch == 0) {
                    // We never had a valid time — go to error screen
                    g_state = STATE_NO_TIME;
                    g_needs_display_update = true;
                } else {
                    // Re-sync failed — carry on with the drifting clock
                    g_state = STATE_RUNNING;
                    g_last_sync_failed = true;
                    g_needs_display_update = true;
                }
            }
            break;
        }

        case STATE_RUNNING: {
            // Check if it's time to enter sleep mode (11pm-5am)
            int32_t local_sec = (g_epoch + g_tz_offset) % 86400;
            if (local_sec < 0) local_sec += 86400;
            int local_hour = local_sec / 3600;
            if ((local_hour >= SLEEP_START_HOUR || local_hour < SLEEP_END_HOUR)
                && now >= g_awake_until) {
                g_state = STATE_SLEEPING;
                enterSleepMode();
                break;
            }

            // Time for a periodic re-sync?
            if (now - g_last_sync_millis >= SYNC_INTERVAL) {
                g_state = STATE_RESYNCING;
                g_needs_display_update = true;
                startSyncAttempt();
            }
            break;
        }

        case STATE_SLEEPING:
            // Not reached (enterSleepMode() is blocking), but handle safely
            // if a wake event occurs outside the sleep block.
            if (now >= g_awake_until) {
                g_state = STATE_RESYNCING;
                g_needs_display_update = true;
                startSyncAttempt();
            }
            break;

        case STATE_NO_TIME:
        default:
            break;
    }

    // ---------------------------------------------------------------
    // 4. Timekeeping: advance the epoch by 60 seconds once per minute,
    //    synced to the strict minute boundary (:00).
    // ---------------------------------------------------------------
    if (g_state == STATE_RUNNING) {
        uint32_t elapsed = now - g_last_tick_millis;
        if (elapsed >= TICK_INTERVAL) {
            // After a sync that arrived mid-minute (g_second > 0), the first
            // advance is partial — we skip ahead to :00.  Every subsequent
            // advance adds exactly 60 seconds (one full minute).
            uint32_t advance = 60;
            if (g_second > 0) {
                advance = 60 - g_second;
            }
            g_epoch += advance;
            updateTimeStruct();
            g_last_tick_millis += TICK_INTERVAL;
            g_needs_display_update = true;
        }
    }

    // ---------------------------------------------------------------
    // 5. Refresh the ePaper display
    // ---------------------------------------------------------------
    if (g_needs_display_update) {
        drawClockFace();
        g_needs_display_update = false;
    }

    // ---------------------------------------------------------------
    // 6. Power Management (System ON Sleep)
    // ---------------------------------------------------------------
    if (g_state == STATE_RUNNING) {
        // Sleep until the next minute boundary. A GPIOTE interrupt on any
        // button pin releases the semaphore and wakes the CPU early.
        uint32_t elapsed = now - g_last_tick_millis;
        uint32_t time_to_tick = (elapsed >= TICK_INTERVAL) ? 0 : (TICK_INTERVAL - elapsed);
        if (time_to_tick > 0) {
            g_button_sem.try_acquire_for(std::chrono::milliseconds(time_to_tick));
        }
    } else {
        // While syncing, avoid WFE sleep as it causes BlueZ to drop the
        // connection. Yield to the mbed RTOS so the BLE stack isn't starved.
        yield();
    }
}