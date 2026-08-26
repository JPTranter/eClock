// Arduino.cpp — host-test mock implementations for the Arduino core API.

#include "Arduino.h"

#include <vector>

// ---- FakeClock state ---------------------------------------------------------
static uint32_t g_millis = 0;

void test_set_millis(uint32_t ms) { g_millis = ms; }
void test_add_millis(uint32_t ms) { g_millis += ms; }
uint32_t millis(void) { return g_millis; }
uint32_t micros(void) { return g_millis * 1000UL; }

// ---- call log -----------------------------------------------------------------
static std::vector<PinEvent> g_pin_log;

void test_reset_pin_log() { g_pin_log.clear(); }
int  test_pin_log_size() { return static_cast<int>(g_pin_log.size()); }
const PinEvent* test_pin_log(int i) {
    if (i < 0 || i >= static_cast<int>(g_pin_log.size())) return nullptr;
    return &g_pin_log[i];
}

static void log_event(PinOp op, int pin, int value) {
    PinEvent e;
    e.op = op;
    e.pin = pin;
    e.value = value;
    g_pin_log.push_back(e);
}

// ---- core functions -----------------------------------------------------------
void pinMode(int pin, int mode) { log_event(PinOp::PIN_MODE, pin, mode); }
void digitalWrite(int pin, int value) { log_event(PinOp::DIGITAL_WRITE, pin, value); }
int  digitalRead(int pin) { log_event(PinOp::DIGITAL_READ, pin, 0); return 0; }
int  analogRead(int pin) { log_event(PinOp::ANALOG_READ, pin, 0); return 0; }
void delay(unsigned long) { /* no-op on host; tests drive time via FakeClock */ }
void delayMicroseconds(unsigned int) {}
void yield(void) {}

static voidFuncPtr g_int_cbs[64] = {nullptr};

void attachInterrupt(int pin, voidFuncPtr cb, int mode) {
    (void)mode;
    if (pin >= 0 && pin < 64) g_int_cbs[pin] = cb;
    log_event(PinOp::ATTACH_INT, pin, 0);
}

int digitalPinToInterrupt(int pin) { return pin; }

// Expose the stored ISR so tests can invoke button_isr directly.
voidFuncPtr test_get_isr(int pin) {
    if (pin >= 0 && pin < 64) return g_int_cbs[pin];
    return nullptr;
}
