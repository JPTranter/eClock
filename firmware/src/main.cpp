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
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "FontHuge7b.h"   // 105pt Arial Bold for the time digits (0-9, :)

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

// Time components for display
static uint8_t g_hour = 0;
static uint8_t g_minute = 0;
static uint8_t g_second = 0;

// Sync timing
static uint32_t g_last_sync_millis = 0;          // millis() when time was last written
static uint32_t g_sync_attempt_start = 0;        // millis() when current attempt began
static const uint32_t SYNC_INTERVAL = 10 * 60 * 1000UL;   // 10 minutes between re-syncs
static const uint32_t SYNC_TIMEOUT  = 60 * 1000UL;         // 60 seconds max per attempt

static uint8_t g_last_sync_hour = 0;
static uint8_t g_last_sync_minute = 0;
static bool g_last_sync_failed = false;

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
    g_second = local_time % 60;
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

static int getBatteryPercent() {
    Serial.println("getBatteryPercent: start");
    // Enable battery divider
    pinMode(14, OUTPUT);
    digitalWrite(14, LOW);
    delay(10); // stabilize
    
    Serial.println("getBatteryPercent: configuring ADC");
    // analogRead(32) crashes in Seeed's mbed core due to an out-of-bounds array access.
    // Instead, we directly instantiate an mbed AnalogIn on P0_31.
    int raw = 0;
    {
        mbed::AnalogIn vbat_adc(P0_31);
        raw = vbat_adc.read_u16() >> 4; // 16-bit down to 12-bit
    }
    
    Serial.println("getBatteryPercent: read complete, disabling divider");
    // Disable divider
    digitalWrite(14, HIGH);
    
    Serial.println("getBatteryPercent: recreating pinMode");
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
    
    Serial.print("getBatteryPercent: done, pct=");
    Serial.println(pct);
    return pct;
}

static void drawClockFace() {
    Serial.println("drawClockFace: start");
    display.setPartialWindow(0, 0, display.width(), display.height());
    Serial.println("drawClockFace: setPartialWindow done, calling firstPage()");
    display.firstPage();
    Serial.println("drawClockFace: firstPage() done");
    do {
        Serial.println("drawClockFace: loop start");
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
                                  (display.height() + mh) / 2 - 8);
                display.print(msg);
                
                break;
            }

            // ---------------------------------------------------------------
            // RUNNING: big single-line time, status below
            // ---------------------------------------------------------------
            case STATE_RUNNING:
            draw_running_face: {
                char timeBuf[8];
                snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", g_hour, g_minute);

                // Calculate date string
                time_t t = g_epoch + g_tz_offset;
                struct tm *tm_info = gmtime(&t);
                char dateBuf[32];
                strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", tm_info);

                // Date line at the top left
                display.setFont(&FreeSans9pt7b);
                display.setCursor(4, 18);
                display.print(dateBuf);
                
                // Battery line at the top right
                char batBuf[8];
                snprintf(batBuf, sizeof(batBuf), "%d%%", getBatteryPercent());
                int16_t bx, by;
                uint16_t bw, bh;
                display.getTextBounds(batBuf, 0, 0, &bx, &by, &bw, &bh);
                display.setCursor(display.width() - bw - 4, 18);
                display.print(batBuf);

                // Use the big 84pt font for the time
                display.setFont(&font);

                // Centre the time string horizontally
                int16_t tx, ty;
                uint16_t tw, th;
                display.getTextBounds(timeBuf, 0, 0, &tx, &ty, &tw, &th);
                
                // Top text bottom edge is ~22. Bottom text top edge is ~110.
                // Available space is 110 - 22 = 88 pixels. Center is 22 + 44 = 66.
                // Time font has th=76, ty=-97. Center of time text relative to baseline is -97 + 38 = -59.
                // So baseline should be 66 - (-59) = 125 to perfectly center the ink between the two rows.
                display.setCursor((display.width() - tw) / 2 - tx, 125);
                display.print(timeBuf);

                // Status line at the very bottom right
                char syncBuf[32];
                if (g_state == STATE_RESYNCING || g_state == STATE_SYNCING) {
                    snprintf(syncBuf, sizeof(syncBuf), "Syncing...");
                } else if (g_last_sync_failed) {
                    snprintf(syncBuf, sizeof(syncBuf), "Sync failed");
                } else {
                    snprintf(syncBuf, sizeof(syncBuf), "Synced %02u:%02u", g_last_sync_hour, g_last_sync_minute);
                }
                
                display.setFont(&FreeSans9pt7b);
                int16_t sx, sy;
                uint16_t sw, sh;
                display.getTextBounds(syncBuf, 0, 0, &sx, &sy, &sw, &sh);
                display.setCursor(display.width() - sw - 4, display.height() - 5);
                display.print(syncBuf);
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
            updateTimeStruct();
            g_last_tick_millis = now; // Prevent immediate +1 second jump
            g_last_sync_hour = g_hour;
            g_last_sync_minute = g_minute;
            g_last_sync_failed = false;
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
    static bool button_was_pressed = false;
    bool button_is_pressed = anyButtonPressed();
    
    if (button_is_pressed && !button_was_pressed && (now - g_last_button_press > BUTTON_DEBOUNCE_MS)) {
        g_last_button_press = now;
        Serial.println("Button pressed — starting manual re-sync...");

        // Only trigger if we aren't already in the middle of a sync attempt
        if (g_state != STATE_SYNCING && g_state != STATE_RESYNCING) {
            g_state = STATE_RESYNCING;
            g_needs_display_update = true;
            startSyncAttempt();
        }
    }
    button_was_pressed = button_is_pressed;

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
                    g_last_sync_failed = true;
                    g_needs_display_update = true;
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

        // Update display every minute when seconds hit 0
        if (g_second == 0) {
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
    // Calculate maximum time we can safely sleep while waiting for the next event.
    // mbed's delay() automatically yields to the RTOS, placing the CPU in low-power WFE.
    uint32_t sleep_time = 100; // Default fast polling for button latency

    if (g_state == STATE_RUNNING) {
        // We can sleep until the next 1-second tick
        uint32_t elapsed_tick = millis() - g_last_tick_millis;
        uint32_t time_to_tick = (elapsed_tick >= 1000) ? 0 : (1000 - elapsed_tick);
        
        // Don't sleep longer than 100ms so buttons remain responsive
        sleep_time = min(time_to_tick, (uint32_t)100);
    } else {
        // While syncing, DO NOT sleep deeply (WFE) as it causes BlueZ to drop
        // the connection. Instead, just yield to the mbed RTOS so Bluetooth
        // background threads aren't starved.
        sleep_time = 0;
        yield();
    }

    if (sleep_time > 0) {
        delay(sleep_time);
    }
}