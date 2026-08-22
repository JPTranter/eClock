# EN05 + XIAO nRF52840 Pinout and Board Traps

## CRITICAL: there are two incompatible hardware generations

The wiring changed completely between board revisions, and using the wrong mapping
produces a panel that ignores everything with `Busy Timeout!`.

| | Generation A | Generation B |
| --- | --- | --- |
| Boards | Older EN04 / early EN05, XIAO nRF52840 (non-Plus) | Current EN05, XIAO nRF52840 **Plus** |
| Panel connection | Hidden pogo pins under the XIAO | Standard header pins |
| CS | 7 (P1.12) | **D1** |
| DC | 16 (P0.27) on Adafruit core, 15 on mbed | **D3** |
| RST | 11 (P0.26) | **D0** |
| BUSY | 3 (P0.29) | **D2** |
| SCK / MOSI | via pogo pads | **D8 / D10** (default hardware SPI) |
| Panel power | MOSFET gate on D6, must be driven HIGH | No gate — physical power switch |
| IMU pin conflict | Yes (P0.26/P0.27) | No |

**This project's board is Generation B.** Determined empirically on 2026-08-22 with
`firmware/tools/pin_diagnostic`, not from documentation.

### How generation was determined

A pull-up / pull-down scan classifies each pin: one driven by an attached device holds
its level against both resistors, while an unconnected pin follows whichever resistor
is applied. Result:

```
  pin   pullup  pulldown  verdict
  D0     0       0        DRIVEN LOW      <- RST
  D1     1       1        DRIVEN HIGH     <- CS
  D2     1       1        DRIVEN HIGH     <- BUSY
  D3     0       0        DRIVEN LOW      <- DC
  D6     1       0        floating        <- no MOSFET power gate here
  D7     1       0        floating        <- Generation A CS: NOT CONNECTED
  D16    1       0        floating        <- Generation A DC: NOT CONNECTED
```

D7 and D16 floating is conclusive: the Generation A pogo-pin mapping inherited from the
old notes cannot work on this board. Reproduce with:

```bash
pio run -e pindiag -t upload --upload-port <bootloader port>
```

Select the generation in `firmware/include/board_pins.h`; Generation B is the default.
Build with `-DECLOCK_EN05_GEN_A=1` for the older hardware.

## Generation A legacy notes

These remain accurate for the older board and are kept for reference.

### Panel power is behind a MOSFET on D6

Generation A gates the panel's supply rail with a power-control circuit driven by
**D6**. It must be an output driven HIGH before initialising the display:

```cpp
pinMode(D6, OUTPUT);
digitalWrite(D6, HIGH);
delay(10);
```

If D6 floats the panel receives no power. It accepts SPI writes without error and does
nothing, which reads exactly like a software bug.

### The mbed core hardware-peripheral trap

On Generation A, `P0.27` (DC) and `P0.26` (RST) are shared with the onboard IMU's I2C
bus (TWIM) and the red LED. The mbed startup sequence leaves the hardware I2C
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

### Pin macros that do not exist

The Adafruit variant header defines only `D0`–`D10`. There is no `D11` or `D16` macro,
so `#define EPD_DC D16` fails to compile with *"'D16' was not declared in this scope"*.
The pin **indices** are still correct — `g_ADigitalPinMap` in `variant.cpp` maps index
16 to P0.27 and index 11 to P0.26 — so use bare integers.

## Core selection consequences

| | Adafruit core | Seeed mbed core |
| --- | --- | --- |
| Display (Gen B) | Works; D0–D10 identical on both cores | Works; same indices |
| Display (Gen A) | Rock solid with 16/11 | Needs the TWIM-disable hack |
| BLE stack | `Bluefruit52Lib` | `ArduinoBLE` |
| BLE caveat | `Bluefruit.begin()` hard-faults on the factory Seeed bootloader; requires flashing the Adafruit nRF52 UF2 bootloader | Works without reflashing |

A welcome side effect of Generation B: because the panel uses `D0`–`D10`, which both
cores define identically, the pin mapping is no longer core-dependent. That removes one
of the two reasons the core choice was hard.

## Panel identification

- Panel: GDEY029T94 (also marked FPC-A005), 296 x 128, monochrome.
- Controller: **SSD1680**.
- GxEPD2 driver class: `GxEPD2_290_T94_V2`.

## Diagnosing a non-responsive panel

Work through this in order:

1. **Which generation is the board?** Run `pio run -e pindiag`. If D7/D16 float, it is
   Generation B and any pogo-pin mapping is wrong.
2. On Generation A: is D6 driven HIGH?
3. On Generation B: is the **physical power switch on**, and is the display's FPC fully
   seated in its connector? There is no software power gate to check.
4. Are DC and RST using the alias correct for the generation *and* core?
5. On Generation A + mbed: has the TWIM-disable hack run before display init?
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
