// hello_ble.cpp — Isolate whether BLE.init (SoftDevice) causes the descriptor-fail.
//
// hello_usb (Serial.begin) -> clean COM10. hellonoser (no Serial) -> clean COM10.
// The only remaining difference from the full clock is ArduinoBLE + BLE.begin(),
// which initializes the SoftDevice (S140). The SoftDevice owns the USB peripheral
// power/clock on the nRF52840, so if BLE.begin() conflicts with the USB state
// handed off by the bootloader, that's the descriptor-fail.
//
// This test is minimal but calls BLE.begin() exactly like the clock's setup().
// If it reproduces "Device Descriptor Request Failed" while hellonoser did not,
// BLE/SoftDevice init is the root cause.
#include <Arduino.h>
#include <ArduinoBLE.h>

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    // Mirror the clock's BLE init (no Serial.begin, matching the clock).
    bool ok = BLE.begin();
    if (ok) {
        digitalWrite(LED_GREEN, LOW);  // green = BLE OK
    } else {
        digitalWrite(LED_RED, LOW);    // red = BLE failed
    }
    BLE.setLocalName("eClock BLE-test");
}

void loop() {
    BLE.poll();
}
