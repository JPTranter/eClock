#include <Arduino.h>

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"

BLEService eClockService("180D"); // Heart Rate
BLEByteCharacteristic eClockCharacteristic("2A37", BLERead | BLEWrite);

void flash_led(int pin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, LOW);
    delay(200);
    digitalWrite(pin, HIGH);
    delay(200);
  }
}

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  flash_led(LED_RED, 2);

  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 5000) { delay(10); }

  Serial.println("\n--- eClock BLE Starting (mbed + ArduinoBLE) ---");
  flash_led(LED_BLUE, 2);

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
  BLE.setAdvertisedService(eClockService);
  
  eClockService.addCharacteristic(eClockCharacteristic);
  BLE.addService(eClockService);
  
  eClockCharacteristic.writeValue(0);
  
  BLE.advertise();
  
  Serial.println("BLE Advertising started successfully!");
  flash_led(LED_GREEN, 3);
}

void loop() {
  BLE.poll();
  digitalWrite(LED_GREEN, LOW);
  delay(100);
  digitalWrite(LED_GREEN, HIGH);
  delay(1900);
}