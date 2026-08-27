# EN05 + XIAO nRF52840 Pinout and Board Traps

## CRITICAL: there are two incompatible hardware generations

The wiring changed completely between board revisions, and using the wrong mapping
produces a panel that ignores everything with `Busy Timeout!`.

| | Generation B (this board) | Legacy (older board) |
| --- | --- | --- |
| Boards | Current EN05, XIAO nRF52840 **Plus** | Older EN04 / early EN05, XIAO nRF52840 (non-Plus) |
| Panel connection | Standard header pins | Hidden pogo pins under the XIAO |
| CS | **D7** (P1.12) | 7 (P1.12) |
| DC | **D16** (P0.31) — shared with VBAT (see §2 in lessons) | 16 (P0.27) on Adafruit core, 15 on mbed |
| RST | **D11** (P0.15) | 11 (P0.26) |
| BUSY | **D3** (P0.29) | 3 (P0.29) |
| SCK / MOSI | **D8 / D10** (default hardware SPI) | via pogo pads |
| Panel power | **MOSFET gate on D6** (P1.11), must be driven HIGH | MOSFET gate on D6, must be driven HIGH |
| IMU pin conflict | No (conflict is D16/VBAT on P0.31) | Yes (P0.26/P0.27) |

**This project's board is Generation B** — but it uses the **D7/D16/D11/D3/D6**
mapping, **with** the D6 MOSFET panel-power gate. It is a XIAO nRF52840 **Plus**,
selected in `firmware/include/board_pins.h` via the vendored
`Seeed_XIAO_nRF52840_Plus` variant (required for the `D16`/`D11` macros). Confirmed on
hardware 2026-08-22; see `docs/lessons/LESSONS_LEARNT.md` §1.

### Verifying the pin mapping

The definitive test is to read the panel pins straight from `firmware/include/board_pins.h`
— that is the source of truth the firmware compiles against:

```cpp
EPD_CS = D7, EPD_DC = D16 (P0.31), EPD_RST = D11 (P0.15),
EPD_BUSY = D3 (P0.29), EPD_POWER = D6 (P1.11, MOSFET gate)
```

If a `Busy Timeout!` appears, first confirm the **Plus variant** is selected (a stock
variant does not define `D16`/`D11`, and the display never responds). A diagnostic
pin-scan harness is available as `pio run -e pindiag`.

## Panel power gate (current board)

The panel's supply rail is behind a MOSFET driven by **D6** (P1.11). It must be an
output driven HIGH before initialising the display:

```cpp
pinMode(D6, OUTPUT);
digitalWrite(D6, HIGH);
delay(10);
```

If D6 floats the panel receives no power. It accepts SPI writes without error and does
nothing, which reads exactly like a software bug. The firmware drives this HIGH in
`setup()` and cuts it LOW in `enterSleepMode()` to save power overnight.

## Legacy board notes (older non-Plus hardware)

These apply to the older EN04 / early EN05 with a non-Plus XIAO, running on the hidden
pogo-pin connection (P0.27 DC / P0.26 RST) with the IMU I2C conflict. Kept for
reference only — the current board does not use this mapping.

### The mbed core hardware-peripheral trap (legacy only)

On the legacy board, `P0.27` (DC) and `P0.26` (RST) are shared with the onboard IMU's
I2C bus (TWIM) and the red LED. The mbed startup sequence leaves the hardware I2C
peripheral enabled on those pins, and on the nRF52 an active hardware peripheral
**completely overrides GPIO toggling**. The display library toggles DC; the I2C
controller silently swallows it; the panel never sees a command.

Release the pins at the very top of `setup()`:

```cpp
NRF_TWIM0->ENABLE = 0; NRF_TWIM1->ENABLE = 0;
NRF_TWI0->ENABLE  = 0; NRF_TWI1->ENABLE  = 0;
NRF_GPIO->PIN_CNF[26] = 3;   // RST back to standard GPIO
NRF_GPIO->PIN_CNF[27] = 3;   // DC  back to standard GPIO
```

This disables the IMU's I2C bus as a side effect. Acceptable for a clock; relevant if
you later want motion wake.

### Pin macros that do not exist (stock variant only)

The **stock** (non-Plus) Adafruit variant header defines only `D0`–`D10`, so there is no
`D11` or `D16` macro and `#define EPD_DC D16` fails with *"'D16' was not declared in this
scope"*. This project uses the vendored `Seeed_XIAO_nRF52840_Plus` variant, which defines
`D11` (P0.15) and `D16` (P0.31) — so this is a non-issue for the current board. It is
only a concern if a stock variant is selected by mistake.

## Core selection consequences

| | Adafruit core | Seeed mbed core |
| --- | --- | --- |
| Display (current Plus board) | Works with `D7`/`D16`/`D11`/`D3` | Works with the same pins (mbed is required for BLE) |
| BLE stack | `Bluefruit52Lib` | `ArduinoBLE` |
| BLE caveat | `Bluefruit.begin()` hard-faults on the factory Seeed bootloader; requires flashing the Adafruit nRF52 UF2 bootloader | Works without reflashing |

The mbed core + ArduinoBLE path was chosen (see `docs/lessons/LESSONS_LEARNT.md` §23) so
the shipped firmware needs no bootloader reflash. The `adafruit` env does not build the
current `main.cpp`.

## Panel identification

- Panel: GDEY029T94 (also marked FPC-A005), 296 × 128, monochrome.
- Operating orientation: **landscape** (`display.setRotation(1)`). The 296-pixel dimension is the horizontal width.
- Controller: **SSD1680**.
- GxEPD2 driver class: `GxEPD2_290_T94_V2`.

## Diagnosing a non-responsive panel

Work through this in order:

1. **Is the Plus variant selected?** The current board needs the vendored
   `Seeed_XIAO_nRF52840_Plus` variant. With a stock variant, `D16`/`D11` are undefined
   and the panel never responds (`Busy Timeout!`). Run `pio run -e pindiag` to scan the
   pins and confirm `CS=D7`, `DC=D16`, `RST=D11`, `BUSY=D3`.
2. **Is D6 driven HIGH?** The panel rail is gated by the D6 MOSFET — if it floats, the
   panel is unpowered and ignores everything.
3. Is the display's FPC fully seated in its connector?
4. Are DC and RST using the correct Plus-variant aliases (`D16` → P0.31, `D11` → P0.15)?
5. On a legacy (non-Plus) board + mbed: has the TWIM-disable hack run before display init?
6. **Read the BUSY pin directly.** On the SSD1680, `1` = busy and `0` = idle. If BUSY
   is stuck at `1` and never falls, no amount of SPI will help — the controller is not
   running. Either it has no power, the FPC is not seated, or the panel is defective.
7. If the panel only **dims slightly** during an update cycle, or ignores commands
   entirely with all of the above correct, suspect a **physically defective panel**.
   This has happened on this project before and replacing the panel resolved it.
   Do not keep debugging software against dead glass.

## Datasheets

Vendor documentation is **not committed** to this repository — it is proprietary to
Seeed Studio and Good Display. See `docs/hardware/datasheets/README.md` for the list of
documents, direct download links, and the expected local filenames.

Everything this project needed from them is captured above, so the repo stands on its
own without them.
