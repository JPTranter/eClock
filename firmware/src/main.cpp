/**
 * eClock - Phase 1: environment and board validation
 *
 * Goal: prove the toolchain, upload path, and board are all working before any
 * clock logic exists. This deliberately does NOT touch the ePaper panel; the
 * display is Phase 2.
 *
 * What it validates:
 *   1. PlatformIO builds and links for the selected core
 *   2. The firmware actually runs on the chip (visible LED blink)
 *   3. USB CDC serial is alive and reports build identity
 *   4. Serial input is received (type any character to get an echo)
 *   5. The D6 panel power gate can be driven (asserted, then released)
 *
 * Success criteria: LED blinks a steady ~1 Hz heartbeat AND the serial monitor
 * prints a banner followed by a once-per-second tick line.
 *
 * See docs/testing/PHASE1_CHECKLIST.md
 */

#include <Arduino.h>
#include "board_pins.h"

// On the Adafruit nRF52 core, `Serial` is a USB CDC endpoint provided by
// Adafruit_TinyUSB, not a hardware UART. Without this include the build
// compiles but fails to link with "undefined reference to `Serial'" and
// friends, because PlatformIO's dependency finder never pulls the bundled
// Adafruit_TinyUSB_Arduino library into the link.
#if defined(ECLOCK_CORE_ADAFRUIT)
  #include <Adafruit_TinyUSB.h>
#endif

// ---------------------------------------------------------------------------
// LED selection
//
// Polarity is NOT assumed. This variant defines LED_STATE_ON (1 = active high),
// so derive both states from it rather than hardcoding. Other cores/boards use
// active-low LEDs and would blink inverted if we guessed.
//
// Prefer the blue LED: LED_RED is pin index 11 = P0.26, which is also the
// ePaper RST line. Blinking RST would be actively misleading later on.
// ---------------------------------------------------------------------------
#if defined(LED_BLUE)
  #define HEARTBEAT_LED LED_BLUE
  #define HEARTBEAT_LED_NAME "LED_BLUE"
#elif defined(LED_BUILTIN)
  #define HEARTBEAT_LED LED_BUILTIN
  #define HEARTBEAT_LED_NAME "LED_BUILTIN"
#else
  #error "No usable onboard LED macro found for this core"
#endif

#if defined(LED_STATE_ON)
  #define LED_ON_STATE  (LED_STATE_ON)
  #define LED_OFF_STATE (!LED_STATE_ON)
#else
  #define LED_ON_STATE  LOW    // most boards are active low
  #define LED_OFF_STATE HIGH
#endif

#if defined(ECLOCK_CORE_ADAFRUIT)
  #define CORE_NAME "Adafruit nRF52"
#elif defined(ECLOCK_CORE_MBED)
  #define CORE_NAME "Seeed mbed"
#else
  #define CORE_NAME "unknown"
#endif

static const uint32_t BLINK_INTERVAL_MS = 400;   // 400ms on, 400ms off = 1.25 Hz
static const uint32_t SERIAL_WAIT_MS    = 3000;  // don't hang if unplugged

static uint32_t g_lastToggle = 0;
static uint32_t g_lastReport = 0;
static uint32_t g_tickCount  = 0;
static bool     g_ledOn      = false;

static void printBanner() {
    Serial.println();
    Serial.println(F("==============================================="));
    Serial.println(F("  eClock - Phase 1 environment validation"));
    Serial.println(F("==============================================="));
    Serial.print(F("  Core       : "));  Serial.println(F(CORE_NAME));
    Serial.print(F("  Built      : "));  Serial.print(F(__DATE__));
    Serial.print(F(" "));                Serial.println(F(__TIME__));
    Serial.print(F("  Heartbeat  : "));  Serial.print(F(HEARTBEAT_LED_NAME));
    Serial.print(F(" @ 1.25 Hz (on="));
    Serial.print(LED_ON_STATE ? F("HIGH") : F("LOW"));
    Serial.println(F(")"));
    Serial.print(F("  Panel pins : CS="));   Serial.print(EPD_CS);
    Serial.print(F(" DC="));                 Serial.print(EPD_DC);
    Serial.print(F(" RST="));                Serial.print(EPD_RST);
    Serial.print(F(" BUSY="));               Serial.print(EPD_BUSY);
    Serial.print(F(" PWR="));                Serial.println(EPD_POWER);
    Serial.println(F("-----------------------------------------------"));
    Serial.println(F("  Expect one tick line per second."));
    Serial.println(F("  Type any character to test the RX path."));
    Serial.println(F("==============================================="));
    Serial.println();
}

void setup() {
#if defined(ECLOCK_CORE_MBED)
    // Must happen before anything touches P0.26/P0.27. Harmless here since the
    // panel is untouched in Phase 1, but keeps the startup order honest.
    eclock_release_twim_pins();
#endif

    pinMode(HEARTBEAT_LED, OUTPUT);
    digitalWrite(HEARTBEAT_LED, LED_OFF_STATE);

    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && (millis() - start) < SERIAL_WAIT_MS) {
        delay(10);
    }

    printBanner();

    // Prove the panel power gate is drivable, then release it. The panel is
    // left unpowered on purpose: Phase 1 does not drive the display, and
    // leaving the rail on would waste current during any sleep testing.
    Serial.print(F("[init] Asserting panel power gate (D"));
    Serial.print(EPD_POWER);
    Serial.print(F(") ... "));
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);
    digitalWrite(EPD_POWER, LOW);
    Serial.println(F("ok, released (panel stays off in Phase 1)"));
    Serial.println();

    g_lastToggle = millis();
    g_lastReport = millis();
}

void loop() {
    const uint32_t now = millis();

    if (now - g_lastToggle >= BLINK_INTERVAL_MS) {
        g_lastToggle = now;
        g_ledOn = !g_ledOn;
        digitalWrite(HEARTBEAT_LED, g_ledOn ? LED_ON_STATE : LED_OFF_STATE);
    }

    if (now - g_lastReport >= 1000) {
        g_lastReport = now;
        g_tickCount++;
        Serial.print(F("[tick "));
        Serial.print(g_tickCount);
        Serial.print(F("] uptime "));
        Serial.print(now / 1000);
        Serial.print(F("s  led="));
        Serial.println(g_ledOn ? F("ON") : F("OFF"));
    }

    // Echo any input so the host -> device serial path is proven too.
    while (Serial.available() > 0) {
        int c = Serial.read();
        Serial.print(F("[echo] received byte 0x"));
        if (c < 0x10) Serial.print(F("0"));
        Serial.print(c, HEX);
        if (c >= 32 && c < 127) {
            Serial.print(F(" ('"));
            Serial.print((char)c);
            Serial.print(F("')"));
        }
        Serial.println();
    }
}
