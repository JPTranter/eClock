#pragma once

// ---------------------------------------------------------------------------
// mbed.h — host-test mock for the mbed OS subset main.cpp uses:
//
//   * rtos::Semaphore(0, 1) + try_acquire() + try_acquire_for() + release()
//   * mbed::AnalogIn(P0_31) with read_u16()
//   * PinName + P0_31
//
// The semaphore is fully scripted so enterSleepMode()'s 6-hour try_acquire_for()
// returns immediately on host. Tests control "woke by button" via the return
// value of the NEXT try_acquire_for() call.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <chrono>

// nRF52 register fakes (NRF_POWER/NRF_TWIM/NRF_TWI/NRF_GPIO) must be visible
// before board_pins.h compiles its eclock_release_twim_pins() inline helper.
#include "nrf52_regs.h"

// ---- PinName ----------------------------------------------------------------
typedef uint32_t PinName;
#define P0_31 (PinName)0x1F   // P0.31 -> index 31

// ---- AnalogIn ---------------------------------------------------------------
// Scriptable analog value (12-bit). getBatteryPercent shifts read_u16() >> 4.
// Declared at global scope (like Arduino.h's test_* helpers) so the harness
// and tests can call them unqualified.
void test_set_analog_u16(uint16_t v);
uint16_t test_get_analog_u16();

namespace mbed {

class AnalogIn {
public:
    AnalogIn(PinName pin) { (void)pin; }
    uint16_t read_u16() { return test_get_analog_u16(); }
};

} // namespace mbed

// ---- rtos::Semaphore ---------------------------------------------------------
// Script the result of the NEXT try_acquire_for() call. true = "woke by button".
// Global scope (see above).
void test_set_next_wake(bool woke_by_button);
bool test_get_next_wake();

namespace rtos {

class Semaphore {
public:
    Semaphore(int32_t count, uint16_t max_count)
        : _count(count), _max(max_count) { (void)_max; }

    void release() {
        if (_count < 1) _count++;
    }

    bool try_acquire() {
        if (_count > 0) { _count--; return true; }
        return false;
    }

    // On host, never blocks; returns the scripted "woke by button" result.
    template <typename Rep, typename Period>
    bool try_acquire_for(std::chrono::duration<Rep, Period>) {
        (void)0;
        return test_get_next_wake();
    }

private:
    int32_t  _count;
    uint16_t _max;
};

} // namespace rtos
