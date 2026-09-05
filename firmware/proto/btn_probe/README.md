# btn_probe — throwaway button probe (kept for lessons)

A minimal dev tool to find out **which** eClock button is dead, **why**, and
whether the fix must be interrupt-based or can be a loop poll. It reproduces
the exact production conditions (mbed core, same GxEPD2 driver + SPI pins,
`display.init()` runs before the buttons are read), then counts each button
with TWO independent counters per button.

Kept permanently as a lessons-learnt instrument. It is deliberately **not**
part of the shipped `src/` firmware and has no unit tests — its "test" is the
on-screen counters and the 115200 serial log.

## Where it lives / how it's wired

```
firmware/proto/btn_probe/src/main.cpp   <- the whole probe
firmware/platformio.ini                 <- [env:proto] points here
```

The [env:proto] build already uses the same mbed core + GxEPD2 display + the
same board pins as the clock (`#include "board_pins.h"` → BUTTON_1/2/3 = D1/D2/D9).
`main.cpp` #defines `ECLOCK_CORE_MBED` itself so it is self-contained.

## Build

```bash
cd firmware
pio run -e proto
```

Produces `.pio/build/proto/firmware.{elf,hex}` (unencumbered, no BLE).

## Flash (same reliable serial-DFU path as the clock)

Bring the board into the bootloader first, then upload to the bootloader port:

```bash
python tools/reset_to_bootloader.py          # 1200-baud touch -> bootloader
pio run -e proto -t upload --upload-port <BOOTLOADER_COM>   # PID 0x0064 port
```

Then open a monitor on the **application** port (115200):

```bash
pio device monitor -e proto --port <APP_COM>              # PID 0x8045 port
```

## What the screen + serial show

Each of the three rows is labeled with its D-pin and shows two live counters:

```
D1  I= 0  P= 0
D2  I= 0  P= 0
D9  I= 0  P= 0
```

- `I` = the **GPIO interrupt** on that pin actually fired (falling edge, debounced ~40 ms)
- `P` = the **loop poll** saw the line active-low (debounced ~40 ms)

Numbers increment as you hold each button (hold ~1 s so the poll and the ISR
both catch it).

## How to interpret the result

| D9 result        | Meaning                                                                 |
|------------------|--------------------------------------------------------------------------|
| `I` rises        | D9 interrupt works — no SPI/MISO blockage. The dead button was *another* one, or a hardware contact fault elsewhere. |
| `I=0` but `P` rises | **MISO/SPI blocks the D9 interrupt** — the interrupt fires but is lost/not routed. Firmware fix: poll D9 and/or force its GPIO config (re-register as plain GPIO input so the pin isn't stuck in SPI MISO mode). |
| `I=0` and `P=0`   | D9 is not electrically making contact. **Hardware fault** on that switch (reflow / solder / wicking). |

Also flags which physical button is which D-pin to confirm the labels.

## Tuning

The probe boots the panel, so it needs `display.init(...)`, which in turn needs
the Plus-variant pin mapping (the same vendored variant the shipped mbed env
uses). It polls every ~10 ms and redraws ~1 s. `SPI_FIRST` (in `main.cpp`) toggles
whether the SPI/display init runs **before** the buttons are attached (`true`,
production order) or **after** (`false`, baseline). Keep `true` to reproduce
the issue.

## Cleaning up

Delete `firmware/proto/btn_probe/` and the `[env:proto]` block in
`firmware/platformio.ini` when the probe has served its purpose. It is not
built by default (the mbed env builds `src/` only) and does not affect the
clock.
