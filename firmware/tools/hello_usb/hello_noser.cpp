// hello_noser.cpp — Isolate the descriptor-fail mechanism.
//
// The full clock firmware does NOT call Serial.begin() (removed for power,
// LESSONS_LEARNT §9). This test is IDENTICAL to hello_usb.cpp EXCEPT it never
// starts the USB CDC (no Serial.begin()). Everything else is the same.
//
// If this app produces "Device Descriptor Request Failed" (while hello_usb,
// which DOES call Serial.begin(), enumerated cleanly as COM10), then the root
// cause is confirmed: after the bootloader hands off, an app that doesn't
// start/replace the USB CDC leaves the host mid-negotiation -> descriptor-fail.
// Fix: either call Serial.begin() (power cost) OR cleanly detach USBDevice so
// the host sees a clean unplug instead of a failed descriptor.
#include <Arduino.h>

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);    // off
    digitalWrite(LED_GREEN, HIGH);  // off
    digitalWrite(LED_BLUE, HIGH);   // off

    // NOTE: deliberately NO Serial.begin() — mirrors the full clock firmware.
    // No USB CDC is started.

    // Blink red so it's visually obvious this app is alive (and distinct from
    // hello_usb which blinked green).
    digitalWrite(LED_RED, LOW);
}

void loop() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 500) {
        last = now;
        digitalWrite(LED_RED, !digitalRead(LED_RED));
    }
}
