# eClock Project: Lessons Learned

> Sections marked "(verified 2026-08-22)" have been confirmed on real hardware from
> this repo. Earlier findings are labelled as "prior art" until re-confirmed. Session
> logs in this directory have per-phase detail.

---

## 1. The XIAO nRF52840 Plus is a different board (verified 2026-08-22)

The standard XIAO nRF52840 variant declares 33 pins; the Plus variant declares 39.
`D11` and `D16` map to entirely different physical ports:

| Symbol | Stock variant (33 pins) | Plus variant (39 pins, this board) |
| --- | --- | --- |
| D11 | index 11 → **P0.26** | index 30 → **P0.15** |
| D16 | **not defined** | index 35 → **P0.31** |

**Without `board_build.variant = Seeed_XIAO_nRF52840_Plus`** the D11/D16 symbols
either fail to compile or resolve to wrong physical pins. GxEPD2 sees no responses and
reports "Busy Timeout!" — indistinguishable from a dead panel.

The Plus variant is vendored at `firmware/variants/`. A compile-time guard in
`board_pins.h` checks `PINS_COUNT == 39` and fails loudly if the wrong variant is
selected.

### EN05 pinout (Plus variant)

| Signal | Symbol | Physical port | Notes |
| --- | --- | --- | --- |
| CS | D7 | P1.12 | Hardware SPI SS |
| DC | D16 | **P0.31** | Shared with PIN_VBAT — read section 2 below |
| RST | D11 | **P0.15** | |
| BUSY | D3 | P0.29 | |
| Panel power gate | D6 | P1.11 | MOSFET, must be HIGH |

### The XIAO has no 32 kHz crystal

`USE_LFRC` is required. Without it the BLE stack's low-frequency clock never starts
and `Bluefruit.begin()` hangs forever.

---

## 2. P0.31 pin conflict: EPD_DC and VBAT (verified 2026-08-22)

`D16` (EPD_DC on the EN05) and `PIN_VBAT` both map to P0.31 in the Plus variant.
`analogRead(PIN_VBAT)` activates the nRF52840's SAADC peripheral with a PSEL claim on
this pin. **Arduino's `pinMode(OUTPUT)` sets the GPIO direction but does NOT clear the
SAADC's PSEL register.** A peripheral pin claim overrides GPIO on the nRF52, so every
subsequent SPI DC toggle silently fails. GxEPD2 reports `_Update_Full : 0` with no
error, and the panel stays frozen.

The fix requires direct register manipulation:

```cpp
// Stop SAADC task and wait
NRF_SAADC->TASKS_STOP = 1;
while (NRF_SAADC->EVENTS_STOPPED == 0) {}
NRF_SAADC->EVENTS_STOPPED = 0;

// Disable the peripheral
NRF_SAADC->ENABLE = 0;

// Clear all 8 channel PSEL registers (0xFFFFFFFF = disconnected)
for (int ch = 0; ch < 8; ch++) {
    NRF_SAADC->CH[ch].PSELP = 0xFFFFFFFFUL;
    NRF_SAADC->CH[ch].PSELN = 0xFFFFFFFFUL;
}

// Reset PIN_CNF directly for GPIO output
NRF_GPIO->PIN_CNF[31] =
    (GPIO_PIN_CNF_DIR_Output   << GPIO_PIN_CNF_DIR_Pos)   |
    (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
    (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos)  |
    (GPIO_PIN_CNF_DRIVE_S0S1   << GPIO_PIN_CNF_DRIVE_Pos) |
    (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

NRF_GPIO->OUTCLR = (1UL << 31);  // LOW = SPI command mode
pinMode(EPD_DC, OUTPUT);         // framework consistency
digitalWrite(EPD_DC, LOW);
```

This is the **minimum register dance** to release a peripheral pin claim on the nRF52.
Call after every `analogRead(PIN_VBAT)`.

---

## 3. Display: GxEPD2 with SSD1680 (verified 2026-08-22)

### Performance

| Operation | Duration | Notes |
| --- | --- | --- |
| Full refresh (black on white) | 3,374 ms | Visible flash |
| Partial refresh | 880 ms avg | 879–881 ms, zero jitter over 53 samples |
| Full-window clear | 3,379 ms | |

**Zero ghosting after 53 consecutive partials.** A full refresh once per hour is a
conservative baseline (24 full + 1416 partials/day = ~22 min active).

### Partial refresh requires the controller to retain its frame buffer

The SSD1680 has volatile internal frame buffer RAM. **Cutting the panel's power rail
(D6 MOSFET) erases it.** GxEPD2's differential partial-update logic compares the new
frame against the old buffer that the controller holds in hardware. If power was cut
between draws, the controller has nothing to diff against.

Rule: init + full draw in one power cycle, then partials can work. Either keep D6
HIGH always, or plan for a 3.4s full re-init after every power-on.

### Construction

```cpp
// Init: full hardware clear, sets rotation
display.init(115200, true, 2, false);
display.setRotation(1);  // landscape: 296w x 128h

// Partial update (fast, no flash):
display.setPartialWindow(0, 0, display.width(), display.height());
display.firstPage();
do { /* draw */ } while (display.nextPage());

// Full update (3.4s, visible flash, clears ghosting):
display.setFullWindow();
display.firstPage();
do { /* draw */ } while (display.nextPage());
```

---

## 4. PlatformIO toolchain (verified 2026-08-22)

### The `_adafruit` board definitions need maxgerhardt's fork

```ini
platform = https://github.com/maxgerhardt/platform-nordicnrf52.git
```

The official PlatformIO `nordicnrf52` platform does not include `xiaoble_adafruit`.

### `Serial` fails to link without TinyUSB

On the Adafruit core `Serial` is USB CDC from `Adafruit_TinyUSB`, not a hardware UART.
PlatformIO's dependency finder will not link the bundled library implicitly. A sketch
using `Serial` compiles then fails at link with `undefined reference to 'Serial'`.

Fix: `#include <Adafruit_TinyUSB.h>`.

### Two USB identities, two different COM ports

| State | VID:PID | | Notes |
| --- | --- | --- | --- |
| Bootloader | `2886:0064` | Accepts serial DFU uploads |
| Application | `2886:8044` | TinyUSB CDC from firmware |

Uploads go to the **bootloader** port; serial monitoring to the **application** port.

### UF2 drag-and-drop is not repeatable

The `XIAO-BOOT` mass-storage volume mounted on first bootloader entry but never
remounted on later resets while DFU serial appeared every time. Use serial DFU.

### Reset-to-bootloader: 1200-baud touch

`firmware/tools/reset_to_bootloader.py` — opens the application port at 1200 baud,
which the bootloader interprets as a reset request. Idempotent: if already in
bootloader it exits cleanly. Double-tap RESET is the manual fallback.

### LED polarity

This variant defines `LED_STATE_ON 1` (active HIGH). Derive from the variant macro
rather than hardcoding. `LED_RED` is D11 = P0.15 — same pin as ePaper RST; do not
blink it in display code. Use `LED_BLUE` (D12 = P0.06).

---

## 5. BLE on nRF52840 (verified 2026-08-22)

### The `Bluefruit.begin()` hard fault is real
The XIAO ships with the factory Seeed UF2 bootloader (e.g., `UF2 0.9.2-29-g6a9a6a3`). The `Bluefruit52Lib` (and raw `sd_ble_*` SoftDevice calls) under the Adafruit core *expect* the custom Adafruit bootloader. Attempting to initialize BLE via the Adafruit core on a stock Seeed bootloader causes an immediate, unrecoverable CPU HardFault.

**Crucial Correction:** The previous theory that simply adding `-D USE_LFRC` would fix the `Bluefruit` crash on the Plus variant was **wrong**. While `USE_LFRC` is necessary for the clock, it does *not* prevent the bootloader SoftDevice conflict. The Adafruit core BLE will always crash on stock Seeed bootloaders.

### The Fix: mbed core + ArduinoBLE
To get BLE working without risking a physical bootloader re-flash, you must use the Seeed mbed core (`board = xiaoble` with `framework-arduino-mbed-seeed`) and the standard `ArduinoBLE` library. 
- Use the `[env:mbed]` environment in `platformio.ini`.
- Disable the `Seeed_XIAO_nRF52840_Plus` custom variant for the mbed build (the variant structure is incompatible with the mbed core out-of-the-box).

### Capturing early boot logs (Terminal Capture)
When dealing with instant HardFaults (like the BLE crash), the board crashes before the host PC can enumerate the USB serial port. To capture early boot logs:
1. Do not rely on standard serial monitors (they give up when the port disappears).
2. Use `firmware/tools/capture_boot.py` which aggressively polls WMI to catch the COM port the millisecond it appears.
3. **PowerShell Loop:** Because the user must physically reset the board to trigger the boot, the 10-second polling timeout in the script might expire before the user acts. Run the script in an infinite PowerShell loop:
   ```powershell
   while ($true) { python firmware/tools/capture_boot.py 5; if ($LASTEXITCODE -eq 0) { break }; Start-Sleep -Seconds 1 }
   ```
   This ensures the script catches the boot banner perfectly, even if the board HardFaults immediately after printing it.

---

## 6. Deprecated: `Seeed_GFX` library

Not used in this project. It cannot do true partial updates (fails to push OLD_COLORS
buffer, causing ghosting), force-sleeps the panel after every update (broken `wake()`
on this panel revision), and has landscape rotation viewport bugs. GxEPD2 is a clean
replacement.

## 7. Deprecated: `D16`/`D11` as `Dxx` macros

The Adafruit stock variant defines only `D0`–`D10`. `D16` and `D11` are not macros
and fail at compile. The pin *indices* are correct (`g_ADigitalPinMap` maps 16→P0.27,
11→P0.26 on the stock variant; 35→P0.31, 30→P0.15 on the Plus variant). This project
uses Dxx symbols with the Plus variant selected, which defines them as `static const`.

---

## Reference resources

- GxEPD2: https://github.com/ZinggJM/GxEPD2
- Seeed Plus variant: vendored at `firmware/variants/Seeed_XIAO_nRF52840_Plus`
- nRF52840 PS reference: SAADC chapter, PSEL registers, GPIO PIN_CNF
- Datasheets: not committed (proprietary); download links in
  `docs/hardware/datasheets/README.md`