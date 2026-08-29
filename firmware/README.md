# Firmware

The PlatformIO project for the eClock. This is the shipped, working firmware — not a
skeleton. It builds on the **mbed core** (`default_envs = mbed`; the `adafruit` env is
kept only for reference and does not compile the current `main.cpp`).

## Layout

```
firmware/
  platformio.ini            Board, core, envs, and library definitions
  src/main.cpp              Application entry point: state machine, BLE sync,
                            ePaper rendering, power management, bottom message
  src/clock_logic.{h,cpp}   Pure, hardware-free time/sleep/battery logic
  src/clock_display.h       Templated ePaper screen renderers (header-only)
  src/FontTitanOne.h        Custom 104pt Titan One font for the time digits
  include/board_pins.h      EN05 pin map (per-core: mbed vs Adafruit)
  include/material_icons.h  Battery / bolt / sync-status icon bitmaps
  include/clock_version.h   Firmware version string (ECLOCK_VERSION)
  variants/                 Vendored XIAO nRF52840 Plus variant (required)
  tools/                    Flash, serial-capture, font/icon generators
  test/                     Host unit-test fixture (compiles main.cpp unmodified)
```

## Build

```bash
cd firmware
pio run -e mbed
```

Flash/RAM usage is printed on a successful build (≈360 KB / 45.4% flash, ≈73 KB /
31.5% RAM).

## Flash (serial DFU is the reliable path on this machine)

```bash
python tools/reset_to_bootloader.py                        # enters the bootloader via 1200-baud touch
pio run -e mbed -t upload --upload-port <bootloader port>  # e.g. COM6
```

If the helper can't find the board, **double-tap the RESET button** to force the
bootloader. See the top-level [README](../README.md) for full build/flash/test steps.

## Host unit tests

`firmware/test/` compiles the unmodified `main.cpp` on the host against mocks
(≥98% line coverage) and dumps real PNG renders of every clock screen. Run:

```bash
cmake -S firmware/test -B firmware/test/build -G Ninja
cmake --build firmware/test/build
ctest --test-dir firmware/test/build --output-on-failure
```

To refresh every documentation screenshot from the current firmware in one shot
(rebuilds, runs the PNG-producing suites, applies the `sleep_icon`/`message` mapping,
assembles the state matrix, and verifies all images), run:

```bash
python firmware/tools/regenerate_docs_screenshots.py
```

## Before writing display code — read the board traps

Read `docs/hardware/PINOUT.md` first. Two things will cost you an evening if you skip
them:

- `D6` (P1.11) must be driven HIGH to power the ePaper panel. If it floats, the panel
  accepts SPI writes and does nothing.
- On the mbed core, the display **RST** pad (P0.15) is not present in the variant's pin
  table. It's re-bound at runtime: `setup()` runs
  `g_APinDescription[29].name = P0_15;` and `EPD_RST` is the remapped index **29**.
  Using a `D11`/`D16` alias here is an *Adafruit-core* convention and will not reach
  P0.15 on mbed. See `docs/hardware/PINOUT.md` §"Verifying the pin mapping" and
  `docs/lessons/LESSONS_LEARNT.md` §5 for the full two-layer index→PinName explanation.
