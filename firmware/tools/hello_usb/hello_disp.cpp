// hello_disp.cpp — Isolate whether the ePaper/display init causes the descriptor-fail.
//
// hello_usb / hellonoser / helloble all enumerate cleanly. The full clock does not.
// The main untested difference is the display init: EPD_POWER MOSFET + GxEPD2
// display.init (SPI) + drawClockFace (renders). Bring up the display exactly like
// the clock and see if it breaks USB enumeration.
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "board_pins.h"

// Match the clock's display object (EN05 panel, Plus variant pins).
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    // Power the ePaper panel (exactly like the clock)
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    // Display init (exactly like the clock)
    display.init(115200, true, 2, false);
    display.setRotation(1);

    digitalWrite(LED_GREEN, LOW);  // green = display init reached
}

void loop() {
    // Draw a simple "hello" screen once to exercise the SPI/panel path
    static bool drawn = false;
    if (!drawn) {
        drawn = true;
        display.setRotation(1);
        display.setTextColor(GxEPD_BLACK);
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(20, 60);
        display.println("hello_disp");
        display.display(false);
    }
}
