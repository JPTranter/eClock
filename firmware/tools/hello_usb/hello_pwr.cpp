// hello_pwr.cpp — Bisect the display-init culprit: EPD_POWER pin only.
// No SPI, no GxEPD2, no panel. Just power the ePaper MOSFET like the clock does.
// If this reproduces the descriptor-fail, the EPD_POWER GPIO (P1.11/D6) is the cause.
#include <Arduino.h>
#include "board_pins.h"

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    // ONLY the EPD_POWER step from the clock's setup()
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    digitalWrite(LED_GREEN, LOW);  // green = reached
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last > 500) { last = millis(); digitalWrite(LED_GREEN, !digitalRead(LED_GREEN)); }
}
