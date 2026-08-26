// mbed.cpp — host-test mock for the mbed OS subset.

#include "mbed.h"

static uint16_t g_analog_u16 = 0xFFFF;   // default: full-scale (max battery)
static bool     g_next_wake  = false;    // default: no button wake (5am wake)

// ---- global-scope scripting hooks (match mbed.h declarations) ----------------
void test_set_analog_u16(uint16_t v) { g_analog_u16 = v; }
uint16_t test_get_analog_u16() { return g_analog_u16; }

void test_set_next_wake(bool woke_by_button) { g_next_wake = woke_by_button; }
bool test_get_next_wake() { return g_next_wake; }
