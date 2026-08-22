/**
 * eClock - Phase 3: power-managed clock firmware
 *
 * Displays HH:MM with battery level on every tick. Partial refresh each minute,
 * full refresh once per hour. Power rail stays on for now (Option A); sleep is
 * a delay() placeholder pending real current measurement.
 *
 * THE SAADC LESSON (P0.31 conflict):
 *   D16 (EPD_DC) and PIN_VBAT both map to P0.31. analogRead() activates the
 *   SAADC peripheral with a PSEL claim on this pin. The Arduino pinMode(OUTPUT)
 *   wrapper does NOT clear that claim. Result: every SPI transaction after any
 *   analogRead() silently fails because the SAADC peripheral overrides the GPIO
 *   toggle for the DC line, and GxEPD2 reports "_Update_Full : 0".
 *
 *   The fix: stop the SAADC task, disable the peripheral, clear all channel
 *   PSEL registers (set to 0xFFFFFFFF = disconnected), then reset PIN_CNF
 *   directly to GPIO output. This is the minimum nRF52840 register dance to
 *   release a peripheral pin claim.
 *
 * Panel: GDEY029T94 / FPC-A005, 296x128 mono, SSD1680 controller.
 * Pins: CS=D7, DC=D16(P0.31), RST=D11(P0.15), BUSY=D3, PWR=D6(P1.11)
 */

#include <Arduino.h>

#if defined(ECLOCK_CORE_ADAFRUIT)
  #include <Adafruit_TinyUSB.h>
#endif

#include "board_pins.h"

#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
static const uint32_t FULL_REFRESH_EVERY = 60;  // partials between full refreshes

// ---------------------------------------------------------------------------
// Battery (with SAADC peripheral release on P0.31)
// ---------------------------------------------------------------------------
static const float BAT_VREF    = 3.6f;
static const float BAT_DIVIDER = 2.0f;

/**
 * Read battery voltage AND release the SAADC peripheral's claim on P0.31.
 *
 * WHY THIS IS NOT A SIMPLE analogRead():
 *   D16 (EPD_DC) and PIN_VBAT both map to physical P0.31. analogRead()
 *   activates the SAADC with a PSEL claim on this pin. pinMode(OUTPUT)
 *   restores GPIO direction but does NOT clear the PSEL register, and an
 *   active peripheral claim overrides GPIO on the nRF52840. The result is
 *   that every subsequent SPI DC toggle silently fails, and GxEPD2 reports
 *   "_Update_Full : 0" with no error.
 *
 *   This function stops the SAADC, disables it, clears all 8 channel PSEL
 *   registers, resets PIN_CNF directly, and then calls pinMode(OUTPUT) so
 *   the Arduino framework's internal tracking stays consistent.
 *
 * Call this ANYTIME — before or after display operations. The SAADC release
 * makes the ordering irrelevant, which is essential for a clock that reads
 * the battery on every tick.
 */
static float readBatteryVoltage() {
    // Switch in the voltage divider
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW);
    delay(1);

    const int raw = analogRead(PIN_VBAT);

    // Disconnect divider to save quiescent current
    digitalWrite(VBAT_ENABLE, HIGH);
    pinMode(VBAT_ENABLE, INPUT);

    // ---- Release P0.31 from the SAADC peripheral ----
    // After analogRead(), the SAADC has a PSEL claim on P0.31 that overrides
    // GPIO operations. Stop the SAADC, disable it, clear all PSEL registers,
    // then reconfigure P0.31 as a standard GPIO output for the DC line.

    // 1. Stop the ongoing SAADC task and wait for acknowledgement
    NRF_SAADC->TASKS_STOP = 1;
    while (NRF_SAADC->EVENTS_STOPPED == 0) {}
    NRF_SAADC->EVENTS_STOPPED = 0;

    // 2. Disable the SAADC peripheral entirely
    NRF_SAADC->ENABLE = 0;

    // 3. Clear PSEL registers for all 8 channels (0xFFFFFFFF = disconnected)
    for (int ch = 0; ch < 8; ch++) {
        NRF_SAADC->CH[ch].PSELP = 0xFFFFFFFFUL;
        NRF_SAADC->CH[ch].PSELN = 0xFFFFFFFFUL;
    }

    // 4. Direct register write to reset P0.31 as GPIO output
    NRF_GPIO->PIN_CNF[31] =
        (GPIO_PIN_CNF_DIR_Output   << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos)  |
        (GPIO_PIN_CNF_DRIVE_S0S1   << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
    NRF_GPIO->OUTCLR = (1UL << 31);  // LOW = command mode

    // 5. Arduino-level restore so framework tracking is consistent
    pinMode(EPD_DC, OUTPUT);
    digitalWrite(EPD_DC, LOW);

    // Convert raw ADC to voltage
    return ((float)raw / 4095.0f) * BAT_VREF * BAT_DIVIDER;
}

/** Crude linear: 4.2V = full, 3.5V = empty */
static uint8_t batteryPercent(float voltage) {
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.5f) return 0;
    return (uint8_t)((voltage - 3.5f) / 0.7f * 100.0f);
}

/** Draw a simple battery icon. */
static void drawBatteryIcon(int16_t x, int16_t y, uint8_t pct) {
    const int16_t w = 22, h = 10;
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    display.fillRect(x + w, y + 3, 2, 4, GxEPD_BLACK);
    if (pct > 0) {
        const int16_t barW = (int16_t)((float)(w - 3) * pct / 100.0f);
        display.fillRect(x + 1, y + 1, barW, h - 2, GxEPD_BLACK);
    }
}

// ---------------------------------------------------------------------------
// Clock state (millis-based; Phase 4 replaces with BLE-synced time)
// ---------------------------------------------------------------------------
static uint32_t g_uptimeS      = 0;
static uint8_t  g_hour         = 0;
static uint8_t  g_minute       = 0;
static uint32_t g_partialCount = 0;
static uint32_t g_fullCount    = 0;
static float    g_batVoltage   = 0.0f;

static void tickTime() {
    g_uptimeS++;
    const uint32_t secs = g_uptimeS;
    g_hour   = (uint8_t)((secs / 3600) % 24);
    g_minute = (uint8_t)((secs / 60) % 60);
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

static void drawClockFace(bool full) {
    if (full) {
        display.setFullWindow();
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Time: centred, large
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", g_hour, g_minute);
        display.setFont(&FreeSansBold24pt7b);
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(timeBuf, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((display.width() - bw) / 2 - bx,
                          (display.height() + bh) / 2 - 8);
        display.print(timeBuf);

        // Status line: battery icon + voltage + refresh counters
        display.setFont(&FreeSans9pt7b);
        display.setCursor(4, display.height() - 5);

        const uint8_t batPct = batteryPercent(g_batVoltage);
        drawBatteryIcon(display.getCursorX(), display.getCursorY() - 12, batPct);
        display.setCursor(display.getCursorX() + 28, display.getCursorY());

        display.print(full ? F("F") : F("p"));
        display.print(g_partialCount + (full ? 0 : 1));
        display.print(F("/"));
        display.print(g_fullCount + (full ? 1 : 0));

        display.print(F("  "));
        display.print(g_batVoltage, 2);
        display.print(F("V  "));
        display.print(batPct);
        display.print(F("%"));
    } while (display.nextPage());

    if (full) {
        g_fullCount++;
        g_partialCount = 0;
    } else {
        g_partialCount++;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void setup() {
#if defined(ECLOCK_CORE_MBED)
    eclock_release_twim_pins();
#endif

    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);

    Serial.println();
    Serial.println(F("eClock Phase 3 — power-managed clock"));
    Serial.print(F("Built: "));
    Serial.print(F(__DATE__));
    Serial.print(F(" "));
    Serial.println(F(__TIME__));
    Serial.println(F("  Strategy: panel rail always on (Option A)"));
    Serial.println(F("  Sleep:    delay(5s) placeholder"));
    Serial.println(F("  Battery:  SAADC PSEL release on P0.31 after each read"));
    Serial.println();

    // Battery before init — ordering doesn't matter now that the SAADC
    // release is robust, but doing it first is cheapest (no display rail
    // powered yet).
    g_batVoltage = readBatteryVoltage();
    Serial.print(F("[boot] battery: "));
    Serial.print(g_batVoltage, 2);
    Serial.print(F("V ("));
    Serial.print(batteryPercent(g_batVoltage));
    Serial.println(F("%)"));

    // Power the panel once and leave it on.
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    display.init(115200, true, 2, false);  // full hardware clear
    display.setRotation(1);

    tickTime();

    const uint32_t t0 = millis();
    drawClockFace(true);
    Serial.print(F("[boot] full refresh in "));
    Serial.print(millis() - t0);
    Serial.println(F(" ms"));
}

void loop() {
    // PLACEHOLDER: not true sleep. Replace with RTC-timed sd_app_evt_wait()
    // after measuring idle current with a multimeter / USB power meter.
    delay(5000);

    tickTime();
    g_batVoltage = readBatteryVoltage();

    if (g_partialCount >= FULL_REFRESH_EVERY) {
        const uint32_t t0 = millis();
        drawClockFace(true);
        Serial.print(F("[full "));
        Serial.print(g_hour);
        Serial.print(F(":"));
        if (g_minute < 10) Serial.print(F("0"));
        Serial.print(g_minute);
        Serial.print(F("] full in "));
        Serial.print(millis() - t0);
        Serial.print(F(" ms  bat="));
        Serial.print(g_batVoltage, 2);
        Serial.println(F("V"));
    } else {
        const uint32_t t0 = millis();
        drawClockFace(false);
        // Verbose log every 5 ticks; silent otherwise
        if (g_uptimeS % 60 == 0) {
            Serial.print(F("[part "));
            Serial.print(g_hour);
            Serial.print(F(":"));
            if (g_minute < 10) Serial.print(F("0"));
            Serial.print(g_minute);
            Serial.print(F("] p"));
            Serial.print(g_partialCount);
            Serial.print(F(" in "));
            Serial.print(millis() - t0);
            Serial.print(F(" ms  bat="));
            Serial.print(g_batVoltage, 2);
            Serial.println(F("V"));
        }
    }
}