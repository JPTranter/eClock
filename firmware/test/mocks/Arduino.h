#pragma once

// ---------------------------------------------------------------------------
// Arduino.h — host-test mock for the Arduino core API.
//
// Provides the surface that main.cpp + Adafruit_GFX touch:
//   pinMode / digitalWrite / digitalRead / analogRead / delay / millis /
//   attachInterrupt / digitalPinToInterrupt, plus String, PROGMEM, F().
//
// pinMode/digitalWrite/digitalRead are recorded to a global call log so tests
// can assert hardware behaviour (e.g. EPD_POWER driven LOW in enterSleepMode).
// millis() is backed by the FakeClock (see harness/FakeClock.h).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <math.h>   // sin/cos/etc. for Adafruit_GFX::rotatePoint

#include "WString.h"

// Adafruit_GFX.h branches on ARDUINO >= 100 to pick the Print-based write API.
#define ARDUINO 100

// Arduino math helpers (Adafruit_GFX uses radians()).
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

// ---- basic types / constants ------------------------------------------------
typedef uint8_t byte;
typedef bool boolean;

#ifndef LOW
#define LOW 0
#define HIGH 1
#endif

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

#define FALLING 3
#define RISING 4
#define CHANGE 5

#define A0 0  // unused by firmware, but harmless

// ---- PROGMEM / pgm_read (x86: identity) -------------------------------------
#define PROGMEM
#define pgm_read_byte(addr)   (*(const unsigned char*)(addr))
#define pgm_read_word(addr)   (*(const unsigned short*)(addr))
#define pgm_read_dword(addr)  (*(const unsigned long*)(addr))
#define pgm_read_float(addr)  (*(const float*)(addr))
#define pgm_read_ptr(addr)    (*(void* const*)(addr))

// F() macro: on host, flash string literals are just RAM strings.
// __FlashStringHelper is a distinct type so Adafruit_GFX's overloads resolve.
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(string_literal))

// ---- button / LED / pin symbols referenced by main.cpp / board_pins.h -------
// Numeric values for the mbed core (indices) — values only matter for the
// call log; hardware does not exist on host. D1/D2/D9 are referenced
// unconditionally by board_pins.h, so they must exist.
#define D1 1
#define D2 2
#define D3 3
#define D6 6
#define D7 7
#define D8 8
#define D9 9
#define D10 10
#define D11 11
#define D16 16

#define LED_RED   (D11 + 1)   // arbitrary; distinct from button pins
#define LED_GREEN (D11 + 2)
#define LED_BLUE  (D11 + 3)

// ---- FakeClock forward decl (implemented in harness/FakeClock.cpp) -----------
// millis() advances via setMillis/addMillis; the sleep tests rely on it.
void     test_set_millis(uint32_t ms);
void     test_add_millis(uint32_t ms);
uint32_t millis(void);
uint32_t micros(void);

// ---- call log (assertable by tests) ------------------------------------------
enum class PinOp { PIN_MODE, DIGITAL_WRITE, DIGITAL_READ, ANALOG_READ, ATTACH_INT };

struct PinEvent {
    PinOp op;
    int   pin;
    int   value;   // mode or HIGH/LOW value
};

void     test_reset_pin_log();
int      test_pin_log_size();
const PinEvent* test_pin_log(int i);

// ---- Arduino core functions (recorded no-ops) --------------------------------
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int  digitalRead(int pin);
int  analogRead(int pin);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield(void);

// Interrupts: recorded, never invoked on host. The ISR is exercised directly
// by the tests (it is a static function in main.cpp).
typedef void (*voidFuncPtr)(void);
void attachInterrupt(int pin, voidFuncPtr cb, int mode);
int  digitalPinToInterrupt(int pin);

// Expose the stored ISR so tests can invoke button_isr directly.
voidFuncPtr test_get_isr(int pin);
