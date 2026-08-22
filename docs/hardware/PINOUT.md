# EN05 + XIAO nRF52840 Pinout and Board Traps

The Seeed XIAO ePaper Display Board (EN05) is not a straightforward SPI breakout.
Three things about it will silently waste hours if you do not know them up front.

## 1. The panel is wired through hidden pogo pins

The EN05 communicates with the ePaper panel using **pogo pins that press against pads
on the underside of the XIAO**, not the visible castellated header pins. You cannot
trace the connection by looking at the headers, and you cannot substitute your own
wiring without lifting the board off the pogo contacts.

## 2. Pin mappings differ per Arduino core

The same physical pad has a different Arduino integer alias depending on which core
you compile against. This is the single most common cause of a display that appears
completely dead.

**The `Dxx` macros do not all exist.** Verified against
`framework-arduinoadafruitnrf52-seeed/variants/Seeed_XIAO_nRF52840/variant.h`: that
header only defines `D0` through `D10`. There is no `D11` and no `D16` macro, so code
written as `#define EPD_DC D16` fails to compile with *"'D16' was not declared in this
scope"*. The pin **indices** 11 and 16 are still correct — `g_ADigitalPinMap` in
`variant.cpp` maps index 16 to P0.27 and index 11 to P0.26 — so use bare integers.

| Signal | nRF52 port pin | Adafruit core index | Seeed mbed core index |
| --- | --- | --- | --- |
| CS | P1.12 | `7` | `7` |
| DC | P0.27 | `16` | `15` |
| RST | P0.26 | `11` | `11` |
| BUSY | P0.29 | `3` | `3` |
| Panel power enable | P1.11 | `6` | `6` |

Two of these pins are shared, which matters later:

- **P0.26 (RST) is also `LED_RED`.** Do not blink the red LED in any sketch that also
  drives the panel.
- **P0.27 (DC) is also the IMU's I2C SCL.** This is the root of the mbed-core trap
  below.

Note that `D16` **does not exist** in the mbed core either. If you port code from the
Adafruit core to mbed without remapping DC to `15`, the display will not respond.

## 3. Panel power is behind a MOSFET on D6

The EN05 gates the panel's supply rail with an active power-control circuit driven by
**D6**. You must configure D6 as an output and drive it HIGH before initialising the
display:

```cpp
pinMode(D6, OUTPUT);
digitalWrite(D6, HIGH);   // Supply power to the ePaper panel
delay(10);
```

If D6 is left floating the panel receives no power. It will accept SPI writes without
error and do absolutely nothing, which reads exactly like a software bug.

## The mbed core hardware-peripheral trap

Even with DC correctly remapped to `15` and RST to `11`, the display can still fail on
the mbed core. `P0.27` and `P0.26` are shared with the onboard IMU's I2C bus (TWIM)
and the red LED. The mbed startup sequence leaves the hardware I2C peripheral enabled
on those pins, and on the nRF52 an active hardware peripheral **completely overrides
GPIO toggling**. The display library dutifully toggles DC; the I2C controller silently
swallows it; the panel never sees a command.

The fix is to forcibly release the pins back to GPIO at the very top of `setup()`:

```cpp
NRF_TWIM0->ENABLE = 0; NRF_TWIM1->ENABLE = 0;
NRF_TWI0->ENABLE  = 0; NRF_TWI1->ENABLE  = 0;
NRF_GPIO->PIN_CNF[26] = 3;   // RST back to standard GPIO
NRF_GPIO->PIN_CNF[27] = 3;   // DC  back to standard GPIO
```

This disables the IMU's I2C bus as a side effect. Acceptable for a clock; relevant if
you later want motion wake.

## Core selection consequences

| | Adafruit core | Seeed mbed core |
| --- | --- | --- |
| Display | Rock solid with `D16`/`D11` | Works only with the TWIM-disable hack |
| BLE stack | `Bluefruit52Lib` | `ArduinoBLE` |
| BLE caveat | `Bluefruit.begin()` hard-faults on the factory Seeed bootloader; requires flashing the Adafruit nRF52 UF2 bootloader | Works without reflashing |
| IMU available | Yes | No, if the hack is applied |

The core decision for this project is deliberately deferred to Phase 1 revalidation —
see `docs/research/RESEARCH_TRACKING.md`.

## Panel identification

- Panel: GDEY029T94 (also marked FPC-A005), 296 x 128, monochrome.
- Controller: **SSD1680**.
- GxEPD2 driver class: `GxEPD2_290_T94_V2`.

## Diagnosing a non-responsive panel

Work through this in order:

1. Is D6 driven HIGH? (Most likely cause.)
2. Are DC and RST using the alias correct for your core?
3. On mbed: has the TWIM-disable hack run before display init?
4. Is BUSY being read on the right pin, and does it ever deassert?
5. If the panel only **dims slightly** during an update cycle, or ignores commands
   entirely with all of the above correct, suspect a **physically defective panel**.
   This has happened on this project before and replacing the panel resolved it.
   Do not keep debugging software against dead glass.

## Datasheets

Vendor documentation is **not committed** to this repository — it is proprietary to
Seeed Studio and Good Display. See `docs/hardware/datasheets/README.md` for the list of
documents, direct download links, and the expected local filenames.

Everything this project needed from them is captured above, so the repo stands on its
own without them.
