#include <Arduino.h>

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"

// Standard Current Time Service UUID
BLEService timeService("1805");

// Current Time Characteristic UUID. 
// Our HA component writes 8 bytes: int32 epoch, int32 tz_offset (little-endian)
BLECharacteristic timeCharacteristic("2A2B", BLEWrite | BLEWriteWithoutResponse | BLENotify | BLERead, 8);

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

  Serial.println("\n--- eClock Phase 4 BLE bringup ---");
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

  BLE.setLocalName("eClock");
  
  // HA component looks for 1805 in the advertising packet
  BLE.setAdvertisedService(timeService);
  
  timeService.addCharacteristic(timeCharacteristic);
  BLE.addService(timeService);
  
  // Provide initial 0-filled 8 bytes
  uint8_t init_val[8] = {0};
  timeCharacteristic.writeValue(init_val, sizeof(init_val));
  
  BLE.advertise();
  
  Serial.print("BLE Advertising started! MAC Address: ");
  Serial.println(BLE.address());
  Serial.println("Waiting for time sync from Home Assistant...");
  flash_led(LED_GREEN, 3);
}

unsigned long last_led_change = 0;
bool led_state = false;

void loop() {
  BLE.poll();

  if (timeCharacteristic.written()) {
    if (timeCharacteristic.valueLength() == 8) {
      int32_t epoch = 0;
      int32_t tz_offset = 0;
      
      const uint8_t* val = timeCharacteristic.value();
      memcpy(&epoch, val, 4);
      memcpy(&tz_offset, val + 4, 4);
      
      Serial.print("Received Time Sync! Epoch: ");
      Serial.print(epoch);
      Serial.print(" | TZ Offset: ");
      Serial.println(tz_offset);

      // Trigger asynchronous rapid flashing in the main loop
      led_state = true;
      last_led_change = millis();
      digitalWrite(LED_GREEN, LOW);
    }
  }

  unsigned long now = millis();
  if (led_state) {
    if (now - last_led_change >= 100) {
      digitalWrite(LED_GREEN, HIGH);
      led_state = false;
      last_led_change = now;
    }
  } else {
    if (now - last_led_change >= 1900) {
      digitalWrite(LED_GREEN, LOW);
      led_state = true;
      last_led_change = now;
    }
  }
}