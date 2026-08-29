// hello_disp_fix.cpp — does a clean USB detach BEFORE display init prevent the
// descriptor-fail? hello_disp.cpp (display init, no USB detach) reproduced the
// "Device Descriptor Request Failed". This is identical but explicitly detaches
// the USB device first. If enumeration is now clean, the fix is to detach USB
// at the top of the clock's setup() before any display/SPI work.
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include "board_pins.h"
#include "USB/PluggableUSBDevice.h"   // for PluggableUSBD().deinit()

GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

void setup() {
    // Cleanly detach USB so the host sees a real unplug (not a failed descriptor)
    // before anything that might perturb the USB peripheral.
    PluggableUSBD().deinit();
    delay(100);

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);

    display.init(115200, true, 2, false);
    display.setRotation(1);

    digitalWrite(LED_GREEN, LOW);
}

void loop() {
    static bool drawn = false;
    if (!drawn) {
        drawn = true;
        display.setRotation(1);
        display.setTextColor(GxEPD_BLACK);
        display.fillScreen(GxEPD_WHITE);
        display.setCursor(20, 60);
        display.println("hello_disp_fix");
        display.display(false);
    }
}
