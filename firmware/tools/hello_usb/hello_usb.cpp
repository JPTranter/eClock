// hello_usb.cpp — minimal test: blink the onboard RGB and say hello over the
// USB CDC serial port. This is the smallest possible firmware that proves the
// board can boot and enumerate its CDC COM port, isolating the clock/BLE/ePaper
// stack out of the picture entirely.
//
// Purpose: after a string of "Device Descriptor Request Failed" errors with the
// full clock firmware, this strips everything back to just a serial hello, so we
// can answer: does the board boot and present a working COM port with ANY code?
//
// RGB is COMMON ANODE (active LOW): LOW = ON, HIGH = OFF.
// Serial on the Seeed mbed core = USB CDC.
#include <Arduino.h>

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);    // off
    digitalWrite(LED_GREEN, HIGH);  // off
    digitalWrite(LED_BLUE, HIGH);   // off

    // Start the USB CDC so this app enumerates as a COM port and can be read.
    Serial.begin(115200);
    // Small settle for the host to enumerate the CDC.
    delay(1500);

    Serial.println();
    Serial.println("eClock hello_usb: booted OK");
    Serial.println("If you can read this, the board boots and its USB CDC works.");
    Serial.flush();

    // Blink green so it's visually obvious the app is alive.
    digitalWrite(LED_GREEN, LOW);
}

void loop() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 1000) {
        last = now;
        Serial.println("hello from eClock hello_usb, tick " + String(millis()));
        Serial.flush();
        // toggle green to show activity
        digitalWrite(LED_GREEN, !digitalRead(LED_GREEN));
    }
}
