# Firmware

Nothing here yet. The PlatformIO project is the first deliverable of Phase 1.

## What goes here

```
firmware/
  platformio.ini        Board, core, and library definitions
  src/main.cpp          Application entry point
  include/              Shared headers (pin definitions, config)
  lib/                  Project-local libraries
  test/                 Unit tests where practical
```

## Before writing display code

Read `docs/hardware/PINOUT.md` first. Two things will cost you an evening if you skip
it:

- `D6` must be driven HIGH to power the ePaper panel. Without it the panel accepts SPI
  writes and does nothing.
- The DC and RST pin aliases differ between the Adafruit and Seeed mbed cores, and the
  mbed core additionally needs the TWIM peripherals disabled before the panel will
  respond at all.

The core has not been chosen yet — see the Arduino core selection entry in
`docs/research/RESEARCH_TRACKING.md`.

## Phase 1 target

A blink test that builds, uploads, and reports over serial from the Windows 11 command
line, with the exact commands captured in the setup documentation so the process is
reproducible.
