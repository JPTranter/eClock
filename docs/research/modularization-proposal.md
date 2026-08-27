# Modularization & Testing Proposal

> Author: 2026-08-25
>
> The firmware has grown to ~700 lines in `main.cpp`. A refactor into
> domain-specific modules would make it easier to maintain, test, and
> extend. This document proposes how to do it safely.

## Current state

`main.cpp` contains 10 logical sections across 699 lines:

| Section | Lines | Functions |
| --- | --- | --- |
| State machine + globals | 32–93 | 17 shared variables |
| Buttons | 95–129 | 3 functions |
| Time helpers | 134–162 | 2 functions |
| Display (battery, clock face, sleep icon) | 164–470 | 4 functions |
| Sleep mode | 421–470 | 1 function |
| setup() | 478–535 | — |
| loop() | 539–699 | — |

All 17 shared variables are `static` (file-scoped). Any split into separate
`.cpp` files must either make them `extern` or bundle them into structs.

## Testing-first approach

Before touching a single line of the firmware, we need a safety net. The
firmware's state machine, time math, and display branching are all *pure
logic* — they depend only on `g_state`, `g_epoch`, `g_tz_offset`, and
`millis()`, not on any hardware.

### Option A: Python simulation (recommended for now)

Write a Python model of the clock's state machine that mirrors the C++
decision logic. It uses the same constants (`TICK_INTERVAL`, `SYNC_TIMEOUT`,
`SLEEP_START_HOUR`, `NIGHT_SLEEP_S`) and the same state transitions.

```python
# test_state_machine.py — runs on host, no board needed
def test_sync_transition():
    clock = Clock()
    assert clock.state == STATE_SYNCING
    clock.write_epoch(1693000000, 36000)
    assert clock.state == STATE_RUNNING

def test_sleep_at_11pm():
    clock = Clock()
    clock.set_local_time(23, 0)
    clock.tick()
    assert clock.state == STATE_SLEEPING

def test_button_wake_cancels_night():
    clock = Clock()
    clock.set_local_time(2, 0)
    clock.enter_sleep()
    clock.press_button()
    assert clock.state == STATE_RESYNCING
    assert clock.awake_until > 0  # stays awake until 11pm

def test_dst_spring_forward():
    # Sleep entered at 23:00, DST springs forward at 02:00
    # Clock should still wake at 05:00 local (fixed 6h sleep)
    clock = Clock()
    clock.enter_sleep_epoch(1693000000, 36000)
    clock.dst_shift(3600)  # spring forward
    clock.wake()
    assert clock.local_hour() == 5

def test_dst_fall_back():
    clock = Clock()
    clock.enter_sleep_epoch(1693000000, 36000)
    clock.dst_shift(-3600)  # fall back
    clock.wake()
    assert clock.local_hour() == 5
```

**Pros:** Quick to write (~1 hour), catches logic bugs, runs in milliseconds.
**Cons:** Doesn't test the actual compiled C++ — only a model of its logic.

### Option B: Host-compiled C++ tests with mocks

Compile the actual firmware source as a host executable against mock headers
that stub out `Arduino.h`, `ArduinoBLE.h`, `GxEPD2_BW.h`, etc. Each mock
is a no-op or a controlled fakes (e.g. `millis()` returns a test counter).

```
firmware/test/
  mocks/
    Arduino.h          # stubs: digitalWrite, pinMode, millis, delay
    ArduinoBLE.h       # stubs: BLE.begin, BLE.address
    GxEPD2_BW.h        # stubs: display.init, setPartialWindow, etc.
    mbed.h             # stubs: rtos::Semaphore, AnalogIn
    board_pins.h       # passthrough: same as real
    material_icons.h   # passthrough: same as real
    FontChango88.h     # passthrough: same as real
  test_state_machine.cpp
  test_time_math.cpp
  test_display.cpp
  CMakeLists.txt       # or meson.build
```

**Pros:** Tests the actual compiled code, catches refactor errors.
**Cons:** ~2 hours to set up the build system and mock headers.

### Option C: On-device tests

Flash test firmware that exercises the state machine and prints results
over serial. Requires manual reset + observation.

**Pros:** Tests the *real* hardware interaction.
**Cons:** Slow (minutes per test cycle), can't test DST edge cases.

### Recommendation

Start with **Option A** (Python simulation) — it catches the class of bugs
that matter most (state machine logic, time math, DST) and is quick to
write. Use it as the safety net for the refactor. If the refactor is
successful, Option B is a natural follow-up for deeper coverage.

## Proposed module structure

After the refactor, `main.cpp` should be ~40 lines of `setup()` + `loop()`.
Everything else lives in domain-specific files:

```
firmware/src/
  clock_globals.h      -- extern state variables, one struct per domain
  clock_ble.h/.cpp     -- BLE characteristic, startSyncAttempt, MAC
  clock_display.h/.cpp -- drawClockFace, drawSleepIcon, getBatteryPercent
  clock_buttons.h/.cpp -- button_isr, initButtons, anyButtonPressed
  clock_sleep.h/.cpp   -- enterSleepMode, NIGHT_SLEEP_S, g_awake_until
  clock_time.h/.cpp    -- updateTimeStruct, minute-boundary alignment
  main.cpp             -- setup() + loop() only
  board_pins.h         -- (unchanged)
  material_icons.h     -- (unchanged)
  FontChango88.h       -- (unchanged)
```

Each module is 80–150 lines, focused on one concern. The shared state lives
in `clock_globals.h` as `extern` variables or small structs — a normal
embedded C++ pattern.

### Refactor workflow

1. Write Python simulation tests (Option A) that cover every state
   transition, sleep/wake edge case, and DST scenario
2. Run the tests against the current firmware to establish a baseline
3. Create `clock_globals.h` — move all `static` variables to `extern`
4. Create each module one at a time, compiling after each
5. Run the tests again — all should pass without changes
6. Flash to hardware for final smoke test
7. Commit

## Open questions

- Should we keep the `static` keyword by using anonymous namespaces
  (`namespace { ... }`) instead of `extern`? Same effect, less invasive.
- PlatformIO doesn't support CMake natively. The test build system would
  be a separate `CMakeLists.txt` in `firmware/test/` — acceptable?
- Should the Python simulation live in `firmware/tools/` (alongside
  `power_budget.py`) or in a new `firmware/test/` directory?

## Resolution (2026-08-27)

This proposal was written before the host C++ test harness existed. That harness
was built instead of the Python simulation, so the refactor took a different,
lower-risk path:

- **Extract the pure logic** into a hardware-free module: `firmware/src/clock_logic.h`
  + `clock_logic.cpp`. It owns time/date math (`splitLocalTime`,
  `localSecondsOfDay`), the sleep-window computation (`isNighttime`,
  `secondsToShutdown`, `kNightSleepSec`), and battery classification
  (`isLowBattery`, `isCriticalBattery`, `kLowBatteryPct`, `kCriticalBatteryPct`).
  It has no Arduino/BLE/GxEPD dependency and is compiled into both the firmware
  build and the host tests.
- **Extract the display rendering** into `firmware/src/clock_display.h`: header-only
  TEMPLATE functions (templated on the display type) so the same source compiles
  against both the real GxEPD2 driver and the host mock. Each screen is a named
  renderer (`drawNoTimeScreen`, `drawSyncingScreen`, `drawRunningFace`,
  `drawSleepingScreen`, `drawSleepIcon`, `drawLowBatteryScreen`) that takes a
  `ClockView` value struct — a pure function of its inputs, with no dependency on
  main.cpp statics, ArduinoBLE, the battery ADC or the board pins.
- **`main.cpp` stays the adapter + wiring layer** (BLE, display, buttons, sleep,
  setup/loop). It builds a `ClockView` from its statics and delegates to
  `clock_logic` and `clock_display`; it keeps its file-scoped `static` symbols so
  the existing harness (which `#include`s `main.cpp`) works unchanged.
- **A dedicated pure test** (`firmware/test/tests/test_clock_logic.cpp`) tests the
  logic module directly, separately from the `main.cpp` hook.

The full multi-file split (clock_ble, clock_display.cpp, etc.) was deliberately NOT
done as separate `.cpp` files: splitting `main.cpp` into many `.cpp`s would make the
harness's `static`-reaching `#include "main.cpp"` impossible. Instead the testable
pieces (logic + display rendering) are extracted as pure functions of their inputs
(`clock_logic` as a linked `.cpp`, `clock_display` as inline template renderers), which
single-sources the parts worth testing without throwing away the 98% coverage harness.

Behavior is unchanged: all host test suites pass, the running/syncing PNG renders are
byte-identical to pre-refactor, and the refactored firmware was flashed and runs on
hardware.
