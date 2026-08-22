#include <Arduino.h>

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "FontLarge7b.h"   // 84pt Arial Bold for the time digits (0-9, :)

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
    STATE_NO_TIME        // Boot sync timed out: no time available (error)
};

static ClockState g_state = STATE_SYNCING;

// ==== Time state ===============================================================
static int32_t  g_epoch = 0;            // seconds since 1970-01-01
static int32_t  g_tz_offset = 0;        // UTC offset in seconds
static uint32_t g_last_tick_millis = 0; // millis() of the most recent 1-second tick
static uint8_t  g_seconds_since_display = 60;  // ticks since last display refresh

// Time components for display
static uint8_t g_hour = 0;
static uint8_t g_minute = 0;

// Sync timing
static uint32_t g_last_sync_millis = 0;          // millis() when time was last written
static uint32_t g_sync_attempt_start = 0;        // millis() when current attempt began
static const uint32_t SYNC_INTERVAL = 10 * 60 * 1000UL;   // 10 minutes between re-syncs
static const uint32_t SYNC_TIMEOUT  = 60 * 1000UL;         // 60 seconds max per attempt

// Display state
static bool g_needs_display_update = true;  // always draw on first loop

// Button debounce
static uint32_t g_last_button_press = 0;
static const uint32_t BUTTON_DEBOUNCE_MS = 300;

// ==== Buttons ==================================================================
// EN05 has three user buttons on D1, D2, D9: active LOW with INPUT_PULLUP.
static void initButtons() {
    pinMode(BUTTON_1, INPUT_PULLUP);
    pinMode(BUTTON_2, INPUT_PULLUP);
    pinMode(BUTTON_3, INPUT_PULLUP);
}

static bool anyButtonPressed() {
    // Active-low: LOW means pressed
    return digitalRead(BUTTON_1) == LOW
        || digitalRead(BUTTON_2) == LOW
        || digitalRead(BUTTON_3) == LOW;
}

// ==== Time helpers =============================================================
static void updateTimeStruct() {
    int32_t local_time = g_epoch + g_tz_offset;
    g_hour = (local_time / 3600) % 24;
    g_minute = (local_time / 60) % 60;
}

static void startSyncAttempt() {
    // Restart BLE advertising so HA can discover and write to us
    // On mbed, re-apply parameters before advertising to ensure the stack actually broadcasts
    BLE.setLocalName("ePaper Clock");
    BLE.setDeviceName("ePaper Clock");
    BLE.setAdvertisedService(timeService);
    BLE.advertise();
    g_sync_attempt_start = millis();
    Serial.println("Sync attempt started (advertising)...");
}

// ==== Display helpers ==========================================================

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
                                  (display.height() + eh) / 2 - 8);
                display.print(errMsg);

                display.setFont(&FreeSans9pt7b);
                display.setCursor(4, display.height() - 5);
                display.print(F("Press any button to sync"));
                break;
            }

            // ---------------------------------------------------------------
            // SYNCING / RESYNCING: waiting for HA to write the time
            // ---------------------------------------------------------------
            case STATE_SYNCING:
            case STATE_RESYNCING: {
                display.setFont(&FreeSansBold24pt7b);
                const char* msg = (g_state == STATE_SYNCING)
                                  ? "Syncing..."
                                  : "Re-sync..";
                int16_t mx, my;
                uint16_t mw, mh;
                display.getTextBounds(msg, 0, 0, &mx, &my, &mw, &mh);
                display.setCursor((display.width() - mw) / 2 - mx,
                                  (display.height() + mh) / 2 - 8);
                display.print(msg);

                display.setFont(&FreeSans9pt7b);
                display.setCursor(4, display.height() - 5);
                display.print(F("Waiting for Home Assistant..."));
                break;
            }

            // ---------------------------------------------------------------
            // RUNNING: big single-line time, status below
            // ---------------------------------------------------------------
            case STATE_RUNNING: {
                char timeBuf[8];
                snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", g_hour, g_minute);

                // Use the big 84pt font for the time
                display.setFont(&font);

                // Centre the time string horizontally and vertically
                // The font's glyph metrics have yOffset ≈ -78, so the baseline
                // for the top of the glyph block is at:
                //   (display.height() + abs(yOffset) - cap_height) / 2 + abs(yOffset)
                // Simpler: centre it so the bulk of the glyph sits in the upper
                // 3/4 of the screen, leaving room for status below.
                int16_t tx, ty;
                uint16_t tw, th;
                display.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
                // th is the full glyph bounding height (~61px for digits)
                // Position it so the top of the time is at y = (128 - th - 20)
                // giving ~20px below for the status line
                display.setCursor((display.width() - tw) / 2 - tx,
                                  (display.height() - th) / 2 - ty);
                display.print(timeBuf);

                // Status line at the very bottom
                display.setFont(&FreeSans9pt7b);
                display.setCursor(4, display.height() - 5);
                display.print(F("Synced via HA"));
                break;
            }
        }
    } while (display.nextPage());
}

// ==== Setup ====================================================================
void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    initButtons();
    Serial.begin(115200);

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

    // Draw the "Syncing..." screen before BLE init so the user sees feedback
    // before the BLE stack (which can take ~1s) finishes starting
    drawClockFace();

    if (!BLE.begin()) {
        Serial.println("FAIL: BLE.begin() failed!");
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

    Serial.println("Boot complete. Advertising for time sync...");
    g_last_tick_millis = millis();
}

// ==== Main loop ================================================================
void loop() {
    BLE.poll();
    uint32_t now = millis();
    static uint32_t last_print = 0;
    if (now - last_print > 5000) {
        last_print = now;
        Serial.print("Liveness: state=");
        Serial.println(g_state);
    }
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'b') {
            Serial.println("Simulating button press via serial...");
            if (g_state != STATE_SYNCING && g_state != STATE_RESYNCING) {
                ClockState old_state = g_state;
                g_state = STATE_RESYNCING;
                g_needs_display_update = (old_state == STATE_NO_TIME);
                startSyncAttempt();
            }
        }
    }
    // ---------------------------------------------------------------
    // 1. BLE characteristic write: Home Assistant sent us the time
    // ---------------------------------------------------------------
    if (timeCharacteristic.written()) {
        if (timeCharacteristic.valueLength() == 8) {
            const uint8_t* val = timeCharacteristic.value();
            memcpy(&g_epoch, val, 4);
            memcpy(&g_tz_offset, val + 4, 4);

            Serial.print("Received time sync! Epoch: ");
            Serial.println(g_epoch);

            // Update state regardless of what it was
            g_state = STATE_RUNNING;
            g_last_sync_millis = now;
            g_seconds_since_display = 60;  // trigger a display update on the next tick
            updateTimeStruct();
            g_needs_display_update = true;

            // Shut down radio to save power now that we have the time
            BLE.stopAdvertise();
            Serial.println("Time synced, radio off.");

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
        Serial.println("Button pressed — starting manual re-sync...");

        // Only trigger if we aren't already in the middle of a sync attempt
        if (g_state != STATE_SYNCING && g_state != STATE_RESYNCING) {
            ClockState old_state = g_state;
            g_state = STATE_RESYNCING;
            g_needs_display_update = (old_state == STATE_NO_TIME);
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
                Serial.println("Sync attempt timed out.");
                BLE.stopAdvertise();

                if (g_epoch == 0) {
                    // We never had a valid time — go to error screen
                    g_state = STATE_NO_TIME;
                    g_needs_display_update = true;
                    Serial.println("Sync failed — no time available.");
                } else {
                    // Re-sync failed — carry on with the drifting clock
                    g_state = STATE_RUNNING;
                    // Don't force a redraw for a timeout — the clock face hasn't changed
                    Serial.println("Re-sync timed out, continuing with existing time.");
                }
            }
            break;
        }

        case STATE_RUNNING: {
            // Time for a periodic re-sync?
            if (now - g_last_sync_millis >= SYNC_INTERVAL) {
                Serial.println("10-minute re-sync interval reached.");
                g_state = STATE_RESYNCING;
                g_needs_display_update = true;
                startSyncAttempt();
            }
            break;
        }

        case STATE_NO_TIME:
        default:
            break;
    }

    // ---------------------------------------------------------------
    // 4. Timekeeping: advance the local epoch clock once per second
    // ---------------------------------------------------------------
    if (g_state == STATE_RUNNING && (now - g_last_tick_millis >= 1000)) {
        g_epoch++;
        updateTimeStruct();
        g_last_tick_millis = now;

        g_seconds_since_display++;
        if (g_seconds_since_display >= 60) {
            g_seconds_since_display = 0;
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
}