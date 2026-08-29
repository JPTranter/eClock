// hello_spi.cpp — Bisect: does SPI.begin() on the display pins break USB?
// hellopwr (power pin) was clean. hellodisp (full GxEPD2 init) failed. The
// intermediate stage is SPI bus bring-up + display GPIO setup. Replicate only
// that (no GxEPD2 driver, no panel command sequence) with raw mbed SPI.
#include <Arduino.h>
#include <mbed.h>
#include "board_pins.h"

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    // Replicate GxEPD2_EPD::init()'s SPI + GPIO setup on the display pins.
    // SPI instance defaults to the board's default SPI pins (what GxEPD2 uses).
    digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);

    SPI.begin();   // the call GxEPD2 makes (may reconfigure pins)

    digitalWrite(EPD_RST, HIGH);
    pinMode(EPD_RST, OUTPUT);
    digitalWrite(EPD_RST, HIGH);

    digitalWrite(EPD_DC, HIGH);
    pinMode(EPD_DC, OUTPUT);
    digitalWrite(EPD_DC, HIGH);

    pinMode(EPD_BUSY, INPUT);

    digitalWrite(LED_GREEN, LOW);  // green = SPI + GPIO stage reached
}

void loop() {
    digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
    delay(500);
}
