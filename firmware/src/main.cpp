#include <Arduino.h>
#include <time.h>
#if defined(ECLOCK_CORE_MBED)
#include <mbed.h>
#endif

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#include "pinDefinitions.h"           // for PinDescription
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"
#include "material_icons.h"
#include "clock_logic.h"
#include "clock_display.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "FontTitanOne.h"  // 104pt Titan One for time digits (0-9, :)

GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Current Time Service BLE UUID
BLEService timeService("1805");

// Current Time Characteristic UUID 0x2A2B.
// HA component writes 8 bytes: int32 epoch, int32 tz_offset (little-endian)
BLECharacteristic timeCharacteristic("2A2B",
    BLEWrite | BLEWriteWithoutResponse | BLENotify | BLERead, 8);

// Bottom-message characteristic (additive; never change the time sync above).
// This is the custom 128-bit UUID shared with the HA integration. HA writes a
// fixed-width, NUL-padded ASCII payload of up to MESSAGE_MAX_CHARS bytes.
// See docs/research/message-feature-design.md.
static constexpr size_t MESSAGE_MAX_CHARS = 30;   // 30 chars = safe before AM/PM
BLECharacteristic messageCharacteristic(
    "c0dec10c-2a2c-4a20-8c10-000000000000",
    BLEWrite | BLEWriteWithoutResponse, (int)MESSAGE_MAX_CHARS);

// ==== Message state ===========================================================
// HA pushes a short message shown on the bottom row. Stored as a NUL-terminated
// C string (the payload arrives NUL-padded). Empty -> nothing shown.
static char g_message[MESSAGE_MAX_CHARS + 1] = {0};

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

// Sleep mode: display off between 11pm and 5am.
//
// A button press during sleep cancels the rest of that night's sleep: the
// clock stays awake through the night and the following day, and shuts down
// again at the next nightly shutdown (11pm). g_awake_until is a millis()
// timestamp ("stay awake until"); it is only non-zero after a button wake.
static uint32_t g_awake_until = 0;
// These exposed constants mirror clock_logic::k* so the host-test harness
// (which #includes main.cpp and references these by name) keeps working, while
// the single source of truth for the values lives in clock_logic.h.
static const uint8_t  SLEEP_START_HOUR = clock_logic::kSleepStartHour;  // 11pm
static const uint8_t  SLEEP_END_HOUR   = clock_logic::kSleepEndHour;    // 5am
// The night sleep window (23:00 -> 05:00) is always 6 hours of wall-clock
// time. We sleep a FIXED duration rather than a epoch-derived target, so a
// DST transition in the middle of the night cannot shift the wake-up hour.
static const uint32_t NIGHT_SLEEP_S = clock_logic::kNightSleepSec;   // 6 * 3600

// Battery thresholds — see clock_logic.h. Below LOW_BATTERY_PCT the clock shows
// the empty-battery icon; at/below CRITICAL_BATTERY_PCT it draws the final
// "LOW BATTERY" screen (which persists on the panel even after power loss).
static const int8_t  LOW_BATTERY_PCT     = clock_logic::kLowBatteryPct;
static const int8_t  CRITICAL_BATTERY_PCT = clock_logic::kCriticalBatteryPct;

// Set true once the clock has drawn the final low-battery screen. It stops the
// loop from redrawing the normal clock face over that message, so the last
// image on the panel is always the unambiguous low-battery warning.
static bool g_low_battery_lock = false;

// True once BLE.begin() has succeeded and BLE.address() is valid.
static bool g_ble_initialized = false;

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
    clock_logic::TimeParts t =
        clock_logic::splitLocalTime(g_epoch, g_tz_offset);
    g_hour = t.hour;
    g_minute = t.minute;
    g_second = t.second;
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
    delayMicroseconds(50); // RC settle time (<10us for 1M divider + small cap)

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

// --- Clock view adapter + screen dispatch ------------------------------------
// Builds a clock_display::ClockView from the firmware's file-scoped statics so
// the rendering (which lives in clock_display.h) is a pure function of this
// snapshot. The MAC string is held in a local so its lifetime spans the draw.

static void drawClockFace() {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        String mac = g_ble_initialized ? BLE.address() : String("");
        clock_display::ClockView v{
            g_hour, g_minute, g_second,
            g_epoch, g_tz_offset,
            g_last_sync_hour, g_last_sync_minute,
            g_last_sync_failed,
            (g_state == STATE_SYNCING || g_state == STATE_RESYNCING),
            g_ble_initialized,
            mac.c_str(),
            getBatteryPercent(),
            ECLOCK_VERSION,
            g_message
        };

        switch (g_state) {
            case STATE_NO_TIME:
                clock_display::drawNoTimeScreen(display);
                break;
            case STATE_SYNCING:
            case STATE_RESYNCING:
                if (g_epoch > 0) {
                    clock_display::drawRunningFace(display, v);
                } else {
                    clock_display::drawSyncingScreen(display, v);
                }
                break;
            case STATE_RUNNING:
                clock_display::drawRunningFace(display, v);
                break;
            case STATE_SLEEPING:
                clock_display::drawSleepingScreen(display);
                break;
        }
    } while (display.nextPage());

    display.powerOff();
}

static void drawSleepIcon() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        clock_display::drawSleepIcon(display);
    } while (display.nextPage());
}

// Final low-battery screen. Drawn once when the cell is critical so the last
// image left on the ePaper panel is an unambiguous "battery empty, charge me"
// message rather than a plausible time a user might trust. Because the panel
// holds its image with no power, this message survives total power loss — a
// dead clock can never be mistaken for a live-but-stopped one.
static void drawLowBatteryScreen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        clock_display::drawLowBatteryScreen(display);
    } while (display.nextPage());
}

static void enterSleepMode() {
    // Draw the real overnight screen (Zzz + "Sleeping") before cutting panel power.
    // NOTE: this is drawSleepIcon(), NOT drawSleepingScreen() — the latter is the
    // Zzz-only *defensive* placeholder used by drawClockFace()'s STATE_SLEEPING case,
    // and is never what actually gets left on the panel overnight.
    drawSleepIcon();
    g_needs_display_update = false;

    // Stop BLE radio
    BLE.stopAdvertise();

    // Power off the ePaper panel (D6 MOSFET gate LOW)
    digitalWrite(EPD_POWER, LOW);
    // Block in WFE sleep for the full night (23:00 -> 05:00 = 6 h).
    // Fixed duration, not epoch-derived, so a DST transition mid-sleep
    // cannot shift the wake-up time. Re-sync with HA on wake.
    bool woke_by_button = false;

    // Compute local seconds-of-day and the "stay awake until next 11pm"
    // target (used only when a button press interrupts the night).
    // The epoch may be stale during DST (off by 1 hour), but the error
    // is bounded and the clock re-syncs immediately on button wake.
    int32_t local_sec = clock_logic::localSecondsOfDay(g_epoch, g_tz_offset);
    uint32_t sec_to_shutdown = clock_logic::secondsToShutdown(local_sec);

    // Sleep for a fixed 6-hour window, immune to DST
    woke_by_button = g_button_sem.try_acquire_for(
                        std::chrono::milliseconds(clock_logic::kNightSleepSec * 1000UL));

    // Re-power the ePaper panel
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);
    display.init(115200, true, 2, false);
    display.setRotation(1);

    // Mbed OS SPI initialization strips the pull-up from the MISO pin (D9).
    // We must forcefully re-apply it so BUTTON_3 doesn't float and cause an IRQ storm.
    pinMode(BUTTON_3, INPUT_PULLUP);

    // If a button woke us, keep the clock awake until the next nightly
    // shutdown (11pm) instead of re-sleeping after a short cooldown.
    if (woke_by_button) {
        g_awake_until = millis() + sec_to_shutdown * 1000UL;
        anyButtonPressed();   // consume any latent button flag
    } else {
        g_awake_until = 0;    // normal 5am wake, allow bedtime check
    }

    // Clear the time — the clock does not know the current time after
    // sleeping (the epoch drifted during the 6h sleep with no RTC). This
    // forces drawClockFace() to show a sync screen (not a stale running
    // face) until HA provides fresh time. A failed re-sync after wake
    // would also re-enter SLEEPING with the stale time still showing,
    // because isNighttime() would be true; clearing g_epoch makes the
    // RESYNCING timeout path go to NO_TIME instead (the honest state).
    // g_epoch is restored on the next successful BLE time write.
    g_epoch = 0;
    g_last_sync_failed = false;

    // Transition to re-sync to get fresh time from HA
    g_state = STATE_RESYNCING;
    g_needs_display_update = true;
    startSyncAttempt();
}

// ==== Tick interval (60 seconds) ==============================================
static const uint32_t TICK_INTERVAL = 60000;  // Advance time once per minute

// ==== Setup ====================================================================
void setup() {
    // Enable internal DC-DC converter (REG1) for ~30-45% reduction in active CPU/radio power
    NRF_POWER->DCDCEN = 1;

    // The mbed SEEED_XIAO_NRF52840_SENSE variant does not define the Plus board's
    // extra pins (P0.15 / P0.31). We dynamically map P0.15 over the unused D29
    // (index 29) so GxEPD2 can toggle EPD_RST (P0.15) natively using Arduino indices.
#if defined(ECLOCK_CORE_MBED)
    extern PinDescription g_APinDescription[];
    g_APinDescription[29].name = P0_15;
#endif

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

g_ble_initialized = true;

    // Redraw after BLE init so the MAC address is valid
    drawClockFace();

    BLE.setLocalName("ePaper Clock");
    BLE.setDeviceName("ePaper Clock");
    BLE.setAdvertisedService(timeService);
    timeService.addCharacteristic(timeCharacteristic);
    timeService.addCharacteristic(messageCharacteristic);
    BLE.addService(timeService);

    uint8_t init_val[8] = {0};
    timeCharacteristic.writeValue(init_val, sizeof(init_val));

    uint8_t init_msg[MESSAGE_MAX_CHARS] = {0};
    messageCharacteristic.writeValue(init_msg, sizeof(init_msg));

    // Begin first sync attempt — the state is already STATE_SYNCING
    startSyncAttempt();

    g_last_tick_millis = millis();
}

static void processBLECommands(uint32_t now) {
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
    // 1b. BLE characteristic write: Home Assistant sent us a bottom message.
    //     Additive and independent of the time sync. Empty (all-NUL) clears it.
    // ---------------------------------------------------------------
    if (messageCharacteristic.written()) {
        int len = messageCharacteristic.valueLength();
        const uint8_t* val = messageCharacteristic.value();
        // Copy at most MESSAGE_MAX_CHARS bytes, stopping at the first NUL (the
        // payload arrives NUL-padded from HA). Ensure NUL-termination.
        size_t n = (len < (int)MESSAGE_MAX_CHARS) ? (size_t)len : MESSAGE_MAX_CHARS;
        size_t i = 0;
        for (; i < n; ++i) {
            g_message[i] = (char)val[i];
            if (val[i] == '\0') break;
        }
        g_message[i] = '\0';
        g_needs_display_update = true;
    }
}

static void processButtonInputs(uint32_t now) {
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
}

static void updateStateMachine(uint32_t now) {
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
                    // Re-sync failed — carry on with the drifting clock.
                    // Push g_last_sync_millis forward to now so the hourly gate
                    // (now - g_last_sync_millis >= SYNC_INTERVAL) is not still
                    // satisfied, and the clock backs off a FULL hour before
                    // trying again. Without this, a failed attempt leaves the
                    // gate true and the clock immediately re-enters RESYNCING
                    // on the next loop() iteration — a tight retry loop that
                    // keeps the radio advertising and drains the battery.
                    // A button press still forces a manual re-sync (see §2).
                    g_state = STATE_RUNNING;
                    g_last_sync_failed = true;
                    g_last_sync_millis = millis();
                    g_needs_display_update = true;
                }
            }
            break;
        }

        case STATE_RUNNING: {
            // Critically low battery: draw the final low-battery screen once,
            // stop the radio, and set the lock so nothing redraws over it. The
            // panel keeps that message even after the cell dies, so a dead clock
            // never shows a stale, trusting time. A recharge reboots the board
            // fresh into STATE_SYNCING (setup() always starts a clean sync).
            // getBatteryPercent() returns -1 on USB power; skip the low-battery
            // check entirely in that case so a USB-powered clock never shows the
            // LOW BATTERY screen (a -1 would otherwise satisfy `<= 5`).
            int batPct = getBatteryPercent();
            if (!g_low_battery_lock && batPct >= 0 &&
                clock_logic::isCriticalBattery(batPct)) {
                g_low_battery_lock = true;
                BLE.stopAdvertise();
                drawLowBatteryScreen();
                g_needs_display_update = false;
                break;
            }

            // Check if it's time to enter sleep mode (11pm-5am). The (int32_t)
            // cast on the "awake until" comparison makes it immune to millis()
            // wraparound (which happens ~49.7 days after boot).
            int32_t local_sec = clock_logic::localSecondsOfDay(g_epoch, g_tz_offset);
            if (clock_logic::isNighttime(local_sec) &&
                (int32_t)(now - g_awake_until) >= 0) {
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
            if ((int32_t)(now - g_awake_until) >= 0) {
                g_state = STATE_RESYNCING;
                g_needs_display_update = true;
                startSyncAttempt();
            }
            break;

        case STATE_NO_TIME:
        default:
            break;
    }
}

static void advanceTimekeeping(uint32_t now) {
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
}

static void refreshDisplayIfNeeded() {
    // ---------------------------------------------------------------
    // 5. Refresh the ePaper display
    // ---------------------------------------------------------------
    // Once the low-battery lock is set, never redraw — the last image on the
    // panel must stay the unambiguous low-battery warning.
    if (g_needs_display_update && !g_low_battery_lock) {
        drawClockFace();
        g_needs_display_update = false;
    }
}

static void managePowerState(uint32_t now) {
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

// ==== Main loop ================================================================
void loop() {
    BLE.poll();
    uint32_t now = millis();

    processBLECommands(now);
    processButtonInputs(now);
    updateStateMachine(now);
    advanceTimekeeping(now);
    refreshDisplayIfNeeded();
    managePowerState(now);
}
