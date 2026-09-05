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

> **These `Dxx` are the Adafruit-core names** (Plus variant). On the **mbed core** the
> firmware uses raw `g_APinDescription` indices: `EPD_CS=7`, `EPD_DC=32 (P0.31)`,
> `EPD_RST=29 (P0.15, runtime-remapped)`, `EPD_BUSY=3`, `EPD_POWER=6` — see §5
> "Getting the Display Working on mbed". The *physical* ports (P1.12/P0.31/P0.15)
> are identical either way.

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
display.setRotation(1);  // landscape — width (296) is the long edge

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
rather than hardcoding. **On the mbed core**, `LED_RED` is D11 = P0.26 (NOT P0.15 —
that's the Adafruit-core's LED naming; on mbed P0.15 is not exposed as an LED and is
used as ePaper RST). Do not blink the RST pin in display code. Use `LED_BLUE`
(D13 = P0.06 on mbed; the Adafruit D12 alias differs).

---

## 5. BLE on nRF52840 (verified 2026-08-22)

### The `Bluefruit.begin()` hard fault is real
The XIAO ships with the factory Seeed UF2 bootloader (e.g., `UF2 0.9.2-29-g6a9a6a3`). The `Bluefruit52Lib` (and raw `sd_ble_*` SoftDevice calls) under the Adafruit core *expect* the custom Adafruit bootloader. Attempting to initialize BLE via the Adafruit core on a stock Seeed bootloader causes an immediate, unrecoverable CPU HardFault.

**Crucial Correction:** The previous theory that simply adding `-D USE_LFRC` would fix the `Bluefruit` crash on the Plus variant was **wrong**. While `USE_LFRC` is necessary for the clock, it does *not* prevent the bootloader SoftDevice conflict. The Adafruit core BLE will always crash on stock Seeed bootloaders.

### The Fix: mbed core + ArduinoBLE
To get BLE working without risking a physical bootloader re-flash, you must use the Seeed mbed core (`board = xiaoble` with `framework-arduino-mbed-seeed`) and the standard `ArduinoBLE` library.
- Use the `[env:mbed]` environment in `platformio.ini`.

### Getting the Display Working on mbed (verified 2026-08-29)

**Why this is needed — the two-layer pin model.** The nRF52 Arduino core translates an
*Arduino pin number* (the index you pass to `digitalWrite`) into a *physical pin*
through a table, `g_APinDescription[]`:

```
Arduino index (uint8_t)  →  g_APinDescription[n].name  →  PinName (P0_15, P0_31, ...)
```

The mbed core's `SEEED_XIAO_NRF52840_SENSE` variant `pinDefinitions.h` builds this table
from the **base SENSE board**, NOT the Plus. That table **lacks `P0_15`** — the physical
pin exists on the Plus board (the display's RST) but is absent from the variant's map.
`P0_31` (DC) **is** present, at index 32. So a `digitalWrite(EPD_RST, ...)` with an
un-remapped index would either go out of bounds (freeze/crash) or drive an *unrelated*
pad; index 29 by default is `P0_9` (an unused NFC/I2C-pull placeholder).

**Do not manually patch `variant.cpp` in `~/.platformio`.** It's outside the repo; an
environment refresh (or a clean CI machine) loses it, resurrecting the crash.

**The Fix (in-repo, portable):** re-bind one *unused* slot of that table at runtime,
before the display initializes. We use Arduino index `29` (default `P0_9` — unused NFC
area) and set it to the physical `P0_15`:

```cpp
// main.cpp, top of setup(), before display.init():
extern PinDescription g_APinDescription[];      // defined in the mbed core
g_APinDescription[29].name = P0_15;             // slot 29 now means "physical P0.15"
```

And in `firmware/include/board_pins.h`, `EPD_RST` must be the **index of the slot you
remapped** (29), not a pre-existing index:

```cpp
#define EPD_RST   29    // P0.15 — dynamically remapped in setup(); do NOT use 33
```

After this, `digitalWrite(29, …)` hits `g_APinDescription[29].name == P0_15` and
correctly drives the physical RST pad. **`D29` here is an Arduino *index*, `P0_15` is
the *physical pin*; the snippet binds them.** `EPD_DC` stays `32` (P0.31 is native in
the mbed table). The other display pins (`CS=7`, `BUSY=3`, `POWER=6`) are unchanged.

> **Why the old `D33` (index 33) was wrong:** earlier firmware used index 33 for
> `EPD_RST`. That index exists in the mbed table but points at **QSPI_SCK (P0.21)** —
> toggling it pulsed the wrong silicon, so the panel behaved inconsistently. The fix
> both makes the correct pad reachable **and** uses a repurposed unused slot.

### ArduinoBLE requires strict non-blocking loops
The `ArduinoBLE` stack on mbed requires frequent calls to `BLE.poll()` to handle background GATT events. **Never use `delay()`** or any blocking functions in the main loop or characteristic callbacks.
If a GATT operation (like a write from Home Assistant) is blocked by a `delay()` for more than a second before `BLE.poll()` can run to send the response, the host BlueZ stack will aggressively time out and tear down the connection. In Home Assistant, this presents confusingly as `[org.freedesktop.DBus.Error.UnknownObject] Method "WriteValue" ... doesn't exist` or `Characteristic ... was not found!`. All animations and display updates must be asynchronous.

### BLE Advertisement Packet Size Limits (31 bytes)
A standard BLE advertisement packet has a hard limit of 31 bytes.
When attempting to advertise a 128-bit Service UUID (18 bytes) alongside the local name `"ePaper Clock"` (14 bytes), the total size (32 bytes) exceeds the limit. `ArduinoBLE` resolves this by silently dropping the Service UUID from the primary advertisement packet.
Because of this, Home Assistant's `BluetoothCallbackMatcher` will fail to trigger if it is configured to match on `service_uuid`. The solution is to configure the Home Assistant integration to match on `name="ePaper Clock"` instead.

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

## 6. UF2 drag-and-drop: now verified working (verified 2026-08-28)

**Update to sections 4/5:** the `XIAO-BOOT` mass-storage drive *does* work for UF2
flashing on this machine, and the auto-reboot behavior is confirmed end-to-end.
The earlier "not repeatable" notes referred to a different issue (the drive not
remounting after a reset) — but when the drive is present, UF2 flashing works:

- `D:/CURRENT.UF2` is the bootloader's dump of the **entire flash** (MBR +
  SoftDevice S140 + bootloader + app). **Never copy over it.**
- The bootloader accepts a UF2 and **auto-reboots into the app ~1 s after a
  complete, valid write**. Filename does NOT matter (spec: only a file *named*
  `CURRENT.UF2` is refused). The `rename to FLASH.UF2` advice is a myth.
- The UF2 must be **canonical spec format**: 8-word header (familyID at byte 24,
  target 0x27000), payload from byte 32, `magicEnd` at byte 508. The old
  hex_to_uf2.py wrote a 36-byte variant (family at byte 32, magic-end at bytes
  36-43, payload at 44) — the bootloader **ignored it** (file copied, no flash,
  no reset). After fixing the converter to the canonical layout, the same image
  flashed and rebooted first try.
- Verified with `firmware/tools/uf2_flash.py` (backs up CURRENT.UF2, copies a
  fresh UF2, watches for the app CDC to appear = flash+reboot confirmed).

Restoring factory state: re-flash the bootloader from
`backups/CURRENT.UF2.bak-*` (the 1.9 MB whole-flash dump).

### Tooling notes
- `firmware/tools/hex_to_uf2.py` now emits canonical UF2 (verified with the
  bootloader on-device).
- `firmware/tools/uf2_flash.py` = backup + copy + watch for auto-reboot. It
  deletes a stale `FLASH.UF2` first (a leftover from a previous attempt makes
  the FAT reject overwrites with `Errno 9`).

## 7. Deprecated: `Seeed_GFX` library

Not used in this project. It cannot do true partial updates (fails to push OLD_COLORS
buffer, causing ghosting), force-sleeps the panel after every update (broken `wake()`
on this panel revision), and has landscape rotation viewport bugs. GxEPD2 is a clean
replacement.

## 42. Deprecated: `D16`/`D11` as `Dxx` macros

The Adafruit stock variant defines only `D0`–`D10`. `D16` and `D11` are not macros
and fail at compile. The pin *indices* are correct (`g_ADigitalPinMap` maps 16→P0.27,
11→P0.26 on the stock variant; 35→P0.31, 30→P0.15 on the Plus variant). This project
uses Dxx symbols with the Plus variant selected, which defines them as `static const`.

> **Note (2026-08-29):** the shipped firmware does **not** rely on the Plus variant's
> `D11`/`D16` aliases for the display RST pin. The mbed core uses the *base* SENSE
> variant's pin table, which **lacks P0.15** (RST) but **does have P0.31 at index 32**
> (so `EPD_DC=32` works natively). `EPD_RST` is the **runtime-remapped index 29**
> (bound to P0.15 in `setup()`); `EPD_BUSY`/`EPD_POWER`/etc. use the indices that exist
> in that table. See §5 "Getting the Display Working on mbed".

---

## 8. Battery monitoring: mbed::AnalogIn vs analogRead (verified 2026-08-23)

On the mbed core, `analogRead(PIN_VBAT)` crashes with an out-of-bounds array access in `Seeed_ADC` because the pin index (32) exceeds the ADC channel table size. The fix is to bypass Arduino's ADC wrapper and instantiate a direct `mbed::AnalogIn`:

```cpp
mbed::AnalogIn vbat_adc(P0_31);
int raw = vbat_adc.read_u16() >> 4;  // 16-bit → 12-bit
```

The `mbed::AnalogIn` object goes out of scope after the read, then `pinMode(32, OUTPUT)` recreates the pin as a GPIO — letting the mbed core properly manage the SAADC peripheral lifecycle instead of manually clobbering registers. This replaced the earlier raw-SAADC-register-dance approach from section 2 when building for the mbed core.

## 9. Power saving: remove Serial entirely and sleep longer (verified 2026-08-23)

### Removing Serial saves UART power

`Serial.begin(115200)` activates the UART peripheral which draws ~1 mA even when idle. Since this is a standalone clock with no host connection during normal operation, removing every `Serial.print()` call and the `Serial.begin()` itself saves meaningful current. All debug info (liveness heartbeats, sync events, battery readings) is eliminated.

### The BLE advertising duty cycle dominates battery life

A BLE advertising session draws ~12 mA. With a 60-second timeout every 10 minutes, the radio is active 10% of the time — averaging 1.2 mA. Changing the re-sync interval from 10 minutes to 1 hour drops this to 1.7% duty cycle (~200 µA average), the single biggest power improvement.

**Further reduction:** shortening `SYNC_TIMEOUT` from 60 to 30 seconds (verified 2026-08-23) halves the advertising window to 30s per hour, giving 0.83% duty cycle (~100 µA average). This costs nothing in reliability — Home Assistant typically responds within 1–3 seconds.

**Estimated runtime with all changes:**

| Battery | 1-min refresh + 1h/60s BLE | 1-min refresh + 1h/30s BLE |
|---------|---------------------------|---------------------------|
| 500 mAh | ~25 days | ~28 days |
| 1000 mAh | ~50 days | ~55 days |
| 2000 mAh | ~99 days | ~111 days |

*Note: reaching 3 months (~90 days) on a 500 mAh cell would also require reducing the display refresh from 1-minute to ~10-minute intervals (~63 µA avg), dropping total average current to ~173 µA.*

## 10. Interrupt-driven buttons with mbed rtos::Semaphore (verified 2026-08-23)

### The polling problem

The original code called `digitalRead()` on three button pins every 100 ms just to detect presses. The CPU did nothing useful during those wake-ups — it polled, found nothing, and went back to sleep. For every 200 µs of active work, the CPU drew ~10 mA, repeated 10 times per second.

### GPIOTE interrupts + semaphore wake

The nRF52840's GPIOTE peripheral generates an interrupt on any configured edge (here, FALLING). The ISR performs hardware-level debouncing (millis() timestamp check) and releases a mbed `rtos::Semaphore`:

```cpp
static void button_isr() {
    static uint32_t last_isr_ms = 0;
    uint32_t now = millis();
    if (now - last_isr_ms > BUTTON_DEBOUNCE_MS) {
        last_isr_ms = now;
        g_button_pending = true;
        g_button_sem.release();    // wakes blocked try_acquire_for()
    }
}
```

The main loop now sleeps by blocking on `g_button_sem.try_acquire_for(chrono::milliseconds(duration))`. Inside mbed's RTOS, this puts the thread to sleep; the idle thread executes WFE (Wait For Event), which is System ON sleep at ~3 µA. When a button is pressed, GPIOTE fires → ISR → semaphore release → scheduler wakes the main thread → try_acquire_for returns instantly.

### Decoupling wake from detection: the volatile flag

There's a subtle problem: `try_acquire_for()` **consumes** the semaphore's count on return. If a button press fired the semaphore *only* to wake the CPU (and the semaphore was consumed by the blocking call), `anyButtonPressed()` would see count=0 and miss the press entirely.

The fix is a separate `volatile bool g_button_pending` flag:

```cpp
// ISR sets both:
g_button_pending = true;   // persists for detection
g_button_sem.release();    // wakes blocked try_acquire_for()

// Detection reads and clears the flag:
bool anyButtonPressed() {
    bool pressed = g_button_pending;
    g_button_pending = false;
    if (pressed) g_button_sem.try_acquire();  // consume leftover semaphore
    return pressed;
}
```

This decouples *wake* (semaphore) from *detection* (volatile flag). No memory barrier needed on single-core Cortex-M4.

### Push the sleep from 100 ms to 60 seconds

With interrupts handling buttons, the CPU no longer needs to poll. The power management block now sleeps up to the **full remaining time** until the next 1-minute tick boundary (up to 60,000 ms). A button press wakes the CPU from WFE in microseconds regardless of sleep depth.

## 11. Minute-boundary time alignment (verified 2026-08-23)

### The problem with 1-second ticks

Previously the clock advanced `g_epoch` by 1 every second, then updated the display only when `g_second == 0`. This meant the CPU woke 60 times per minute but only did useful work once. The remaining 59 wake-ups were pure overhead.

### Advancing by 60 seconds once per minute

Change `TICK_INTERVAL` from 1000 to 60000 and advance `g_epoch` by 60 on each tick:

```cpp
if (elapsed >= TICK_INTERVAL) {
    uint32_t advance = 60;
    if (g_second > 0) {
        advance = 60 - g_second;  // first tick after sync: snap to :00
    }
    g_epoch += advance;
    updateTimeStruct();
    g_last_tick_millis += TICK_INTERVAL;
}
```

### Aligning the first tick to :00

When Home Assistant sends the time at, say, `14:32:17`, we compute the subsecond offset and anchor `g_last_tick_millis` to the current minute boundary:

```cpp
g_last_tick_millis = now - (g_second * 1000);
```

The first tick fires after `60 - 17 = 43` seconds (the partial interval to `:00`), advancing `g_epoch` by 43. Every subsequent tick fires after exactly 60 seconds, advancing by 60. The display always updates on the minute boundary with no jitter.

## 12. Custom fonts with gfxfont_gen.py (verified 2026-08-23)

### Generator workflow

`firmware/tools/gfxfont_gen.py` renders a TrueType font at a given point size and generates an Adafruit GFX-compatible font header containing only the glyphs you request:

```bash
python firmware/tools/gfxfont_gen.py <ttf_path> <pt_size> <first_char> <last_char> <output.h>
```

Store **only the characters the display actually needs** — the clock only renders digits 0-9 and colon (ASCII 0x30-0x3A). For the 84pt Moirai One font, the bitmap data was 4.3 KB vs 33 KB for the full Arial Bold font.

### Glyph alignment

Each glyph in the GFXglyph table has a `yOffset` field (6th column). This is a signed offset from the baseline. When glyphs from the same font have different natural baselines (like a colon that sits higher than digits), you must manually adjust the colon's yOffset for visual vertical centering. For the Moirai 84pt font, the colon was at yOffset=-102 (floating above digits) and needed to be moved to yOffset=-86 to align its midpoint with the digits' midpoint.

### Centering on the ePaper

The `FontHuge7b.h` comment had hardcoded centering math for the Arial Bold 105pt font. After swapping fonts, the `setCursor` y-coordinate must be recalculated from the new font's glyph metrics: typical g.yOffset ≈ -99, typical glyph height ≈ 67, so the ink midpoint is at -99 + 33.5 = -65.5, and the baseline for vertical centering between the date line (bottom at ~22) and status line (top at ~110) is 66 - (-65.5) ≈ 132.

> **Full step-by-step reference with sizing scan script and centering
> calculator:** `docs/reference/FONT_GENERATION.md`

## 13. USB power detection via VBUS register (verified 2026-08-23)

The nRF52840's `POWER` peripheral includes a `USBREGSTATUS` register with a `VBUSDETECT` bit that is set when VBUS voltage is above the valid threshold. This does not require initialising the USB stack — it is a purely hardware level detection available at any time:

```cpp
if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) {
    // USB cable connected — skip battery ADC, show "USB" on display
}
```

**Register location:** `NRF_POWER` (base address 0x40000000), `USBREGSTATUS` at offset 0x00C, bit 0 = `VBUSDETECT`. Definitions come from the CMSIS-SVD headers (`nrf52840_bitfields.h`).

When USB is connected, the battery voltage divider reading is invalid (VBUS overrides the voltage). The `getBatteryPercent()` function returns -1 when VBUS is detected, and the display renders "USB" instead of a percentage.

## 14. Night sleep mode: display off between 11pm and 5am (verified 2026-08-23)

### Motivation

The clock runs fully idle between bedtime and morning — no one reads it, but the ePaper panel refreshes once per minute and BLE re-syncs every hour. Power-gating the panel and disabling BLE for 6 hours saves ~390 µAh per night (~3 days/month on a 500 mAh battery).

### Implementation

A new `STATE_SLEEPING` state is checked in the main loop when `g_state == STATE_RUNNING`. It examines the **local hour** (computed from `g_epoch + g_tz_offset`) and triggers sleep if hour ≥ 23 or hour < 5.

```cpp
int32_t local_sec = (g_epoch + g_tz_offset) % 86400;
if (local_sec < 0) local_sec += 86400;
int local_hour = local_sec / 3600;
if ((local_hour >= SLEEP_START_HOUR || local_hour < SLEEP_END_HOUR)
    && now >= g_awake_until) {
    g_state = STATE_SLEEPING;
    enterSleepMode();
}
```

The `enterSleepMode()` function:
1. Draws a "Zz / Sleeping" screen on the ePaper (last thing the user sees)
2. Calls `BLE.stopAdvertise()` to shut down the radio
3. Drives `EPD_POWER` (D6) LOW to remove power from the entire panel + MOSFET
4. Calculates seconds until 5am: `(5*3600 - local_sec + 86400) % 86400`
5. Blocks on `g_button_sem.try_acquire_for(sleep_ms)` — WFE sleep with a strict timeout
6. On wake (button or timeout), re-powers the panel via `display.init()`, which takes ~3.4s for a full refresh
7. Enters `STATE_RESYNCING` to get fresh time from Home Assistant

### Button wake with cooldown

A button press during sleep wakes the CPU instantly (GPIOTE → semaphore release → `try_acquire_for` returns early). The clock then stays awake for 15 minutes (`WAKE_COOLDOWN = 900000 ms`) before re-entering sleep. This lets the user read the time, then let the clock auto-sleep without wasting the night's power savings.

### Power-gating the ePaper

Key detail: when the panel is unpowered between 11pm and 5am, the SSD1680 controller loses its internal frame buffer. `display.init()` performs a full hardware clear (~3.4s) before any draw. This is the correct trade-off because there is no need for a partial-refresh-speed wake during sleep hours — the 3.4s re-init is invisible to the user who wakes the clock by button press.

### Battery math

| Component | Day (18h) avg | Night (6h) avg | Weighted 24h |
|-----------|-------------|---------------|--------------|
| Display refresh | 625 µA | 0 µA (panel off) | ~469 µA |
| BLE radio | 100 µA | 0 µA (radio off) | ~75 µA |
| Sleep baseline + CPU ticks | 20 µA | 3 µA (pure WFE) | ~16 µA |
| **Total** | **~745 µA** | **~3 µA** | **~560 µA** |

**500 mAh battery:** ~25 days → ~37 days (gain of 12 days/month)
**1000 mAh battery:** ~50 days → ~74 days

*Note: the 15-minute button cooldown adds some awake time during the night, reducing the savings slightly. Each button-press-wake episode costs ~3.5 mAh (3.4s re-init + 15 min of normal operation), so ~1 night of occasional reading.*

## 15. 12-hour display format and font_tool.py (verified 2026-08-23)

### 12-hour conversion

The clock display was changed from 24-hour (`%02u:%02u` with leading zero)
to 12-hour (`%u:%02u` with leading zero suppressed) to match typical
bedside clock expectations:

```cpp
uint8_t disp_hour = g_hour % 12;
if (disp_hour == 0) disp_hour = 12;
const char* ampm = (g_hour < 12) ? "AM" : "PM";
```

The status line ("Synced HH:MM") moved from the **bottom right** to the
**bottom left**, with "AM" / "PM" occupying the bottom right. This avoids
the sync time and AM/PM overlapping when both are rendered in FreeSans9pt.

### 12-hour times are narrower — bigger font

Because the leading zero is suppressed, `8:00` is 4 characters instead of
5, and even `10:00` is only one character wider than `12:34` rather than
a full digit wider. This allowed bumping the font from Chango 80pt
(which barely fit 24-hour `08:02`) to **Chango 82pt** (which fits all
12-hour times with at least 1px margin).

### font_tool.py — the all-in-one font utility

`firmware/tools/font_tool.py` consolidates everything learned across
four font swaps into three commands:

| Command | Function |
|---------|----------|
| `scan` | Scans every 2pt from 60–128, checking both 12h and 24h fit |
| `generate` | Runs gfxfont_gen.py, adds copyright, prints centering math + `setCursor` line |
| `center` | Reads an existing GFXfont header and prints the centering calculation |

The tool automatically detects whether the colon's yOffset needs manual
adjustment (difference >5 px from digit midpoints), and computes the
correct baseline_y for the centering formula.

## 16. Material Icons for status indicators (verified 2026-08-23)

### Why icons instead of text

The status line originally showed "Synced HH:MM", "Syncing...", or
"Sync failed" as text. Replacing the text labels with Material Icons
saves screen space, reads faster at a glance, and eliminates the
width jitter from different-length text strings.

### Icon selection

Three Material Icons were chosen from the official Google Material Icons
font downloaded from GitHub (`MaterialIcons-Regular.ttf`):

| State | Icon | Unicode | Name | Fill |
|-------|------|---------|------|------|
| Synced | ✓ in circle | U+E86C | check_circle | ~55% |
| Syncing | ↻ arrows | U+E5D5 | refresh | ~23% |
| Failed | ✕ in circle | U+E000 | error | ~56% |

The `check_circle` and `error` icons share a matching circular outline,
giving a cohesive look side-by-side.

### Font size and rendering

Icons were extracted from the TTF at **14pt**, then converted to 1-bit
MSB-packed bitmap arrays. 14pt gives good boldness on the 200dpi ePaper
display without anti-aliasing artifacts — the filled-area density is
high enough that strokes are visible.

Key metrics:
- check_circle: 14×13px (26 bytes of bitmap data)
- error: 14×13px (26 bytes)
- refresh: 14×12px (24 bytes)

### Rendering on ePaper

Adafruit GFX's `drawBitmap()` renders raw bitmap data at given
coordinates. Icons sit at the bottom left with the last-sync time
printed next to them using FreeSans9pt7b:

```cpp
int icon_y = display.height() - 5 - icon_h;
display.drawBitmap(4, icon_y, icon_bmp, icon_w, icon_h, GxEPD_BLACK);
display.setCursor(4 + icon_w + 4, display.height() - 5);
display.print(syncTime);
```

The icon bottom edge aligns with the text baseline by subtracting
`icon_h` from the text baseline Y.

### icon_tool.py — icon extraction utility

`firmware/tools/icon_tool.py` extracts any character from any TrueType
icon font and generates a ready-to-use C header with `PROGMEM` bitmap
data, matching the format in `material_icons.h`:

```bash
# Export a single icon
python firmware/tools/icon_tool.py export "C:/path/to/icons.ttf" 14 0xE86C
# Exports: 14pt check_circle → C bitmap array

# Export all named icons from a config
python firmware/tools/icon_tool.py batch "C:/path/to/icons.ttf" 14 --icons \
    "check=0xE86C,error=0xE000,refresh=0xE5D5"
# Exports: complete material_icons.h replacement
```

The tool automatically:
- Renders the glyph at the requested size
- Trims empty rows from the top/bottom (minimizes bitmap size)
- Packs to MSB-first byte arrays
- Outputs in the same format as `material_icons.h`

### Storing bitmaps vs including the full font

The font file `MaterialIcons-Regular.ttf` is ~350 KB — far too large to
embed in firmware (current firmware is ~360 KB total flash including
all code). Instead, only the three needed glyphs are extracted as tiny
manual bitmaps (~148 bytes total). This is the correct trade-off for
constrained flash.

### Icon size and vertical alignment

Icons were first extracted at **14pt** (14×13px, auto-trimmed). The
fixed 1-bit rendering loses anti-aliasing, so the minimum readable size
is about 14pt (where the check circle icon has ~55% pixel fill). Going
higher to **17pt** gives larger glyphs (17×16 for circle icons vs the
~11px height of FreeSans9pt text) and is a better visual match for the
status line.

When positioning an icon next to text rendered by FreeSans9pt7b:

```cpp
// DON'T do this (icon extends above text):
int icon_y = display.height() - 5 - icon_h;

// DO this (icon top aligns with text top):
int icon_y = display.height() - 5 - 12;  // 12px caps height
```

The icon's bottom edge should not align with the text's baseline.
FreeSans9pt7b's yOffset means text extends upward from the baseline,
so the icon needs a fixed offset from the baseline that matches the
text's cap height (~12px rather than the icon's full height of 17px).

### icon_tool.py — --no-trim flag (added later)

By default, `icon_tool.py` trims empty rows and columns from each
glyph's bounding box to minimise bitmap data. This shrinks the
check_circle from 17×16 to 15×15, which causes alignment to shift
between icons of different shapes. Pass `--no-trim` to preserve the
raw glyph dimensions from the font:

```bash
python icon_tool.py batch "MaterialIcons-Regular.ttf" 17 \
    --icons "synced=0xE86C,failed=0xE000,syncing=0xE5D5" \
    --header material_icons.h --guard "ECLOCK_MATERIAL_ICONS_H" --no-trim
```

This ensures all icons use the same coordinate space, making pixel
positions consistent in `drawBitmap()` calls.

### Material Symbols Outlined — newer icon font

The original `MaterialIcons-Regular.ttf` (Google Material Icons, ~350 KB)
was replaced with `MaterialSymbolsOutlined.ttf` (Material Symbols Outlined,
~10 MB variable font from GitHub) because the new set includes horizontally
oriented icons that are more suitable for the clock's status line:

| Purpose | Old icon (Material Icons) | New icon (Material Symbols Outlined) |
|---------|--------------------------|--------------------------------------|
| USB / charging | `bolt` — vertical zigzag (U+E8B0, 17×16) | `battery_android_bolt` — horizontal battery + bolt inside (U+F305, 20×16) |
| Battery | `battery_std` — vertical battery (U+E19C, 17×16) | `battery_android_full` — horizontal pill battery (U+F304, 20×15) |
| Battery low | *(not available)* | `battery_android_0` — horizontal empty outline (U+F30D, 20×15) |

The status icons (`check_circle`, `error`, `refresh`) share codepoints
between the two font families and are identical in both.

The large Material Symbols font file (~10 MB) is **not** stored in the
repo. Only the extracted bitmap arrays (~200 bytes total) live in
`material_icons.h`. The TTF can be re-downloaded if needed:

```
https://github.com/google/material-design-icons/raw/master/variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf
```

### Sprite sheet tool — visual review before use

`icon_tool.py sprite` renders a PNG sprite sheet of all active icons
for easy visual review before deploying to firmware. Usage:

```bash
python firmware/tools/icon_tool.py sprite "font.ttf" 20 \
    --icons "battery=0xF304,bolt=0xF305" --output preview.png
```

The sprite sheet renders:
- Black-on-white (matching ePaper colour scheme, unlike earlier versions)
- Black border around each icon cell
- Labels with codepoint, name, and pixel dimensions
- Saved to the project root by default

**Mask gotcha (Pillow):** When pasting a `mode='1'` bitmap onto an RGB
background, Pillow interprets `0` as transparent and `1` as opaque.
Since 1-bit images use `fill=0` for black (ink) and `fill=1` for white
(bg), the paste mask is **inverted** — black ink becomes transparent.
Fix by converting to 'L' mode and inverting:

```python
mask = Image.eval(icon_img.convert('L'), lambda x: 255 - x)
icon_rgb.paste((0, 0, 0), mask=mask)  # black ink paste correctly
```

### Dependencies

All Python tools in `firmware/tools/` require:

| Package | Used by | Install |
|---------|---------|---------|
| `Pillow` | `gfxfont_gen.py`, `font_tool.py`, `icon_tool.py` | `pip install Pillow` |
| (stdlib only) | `reset_to_bootloader.py`, `capture_boot.py` | No extra deps |

Pillow is the only non-stdlib dependency across all tools.

## 17. Display layout: 2px margins and centering (verified 2026-08-23)

The ePaper display (296×128) has no drawn border — the physical panel frame
provides the edge. Setting `fillScreen(GxEPD_WHITE)` clears the entire panel.
Content should maintain **2px margins** from each edge to prevent rendering
artifacts near the glass edge.

### Layout constants

```
 0       2                          panel edge
 |       |                          ── 2px margin
 | Date text (x=2, baseline=12)     │ Bolt/battery icon (right-2, y=2)
 |                                   │
 |                                   │  Time zone: 98px
 |     Clock time (centre at 118)    │
 |                                   │
 | Icon (2px left)  Sync time  AM   │  Baseline at h-2-5 ≈ 121
 2                                    ── 2px margin
128                                panel edge
```

### Time centering formula

```
Available space = 114 - 16 = 98 px
Centre = 16 + 49 = 65
yOff = -(glyph_bottom),  ink_h = glyph_height
ink_mid = yOff + ink_h / 2
baseline = 65 - ink_mid
```

Example (Chango 82pt): yOff=-84, ink_h=62 → ink_mid=-53 → baseline=118

### Layout commands in font_tool.py

```
python font_tool.py layout "Chango-Regular.ttf" 82
```

Outputs all positioning constants for the firmware `main.cpp`.

## Reference resources

- GxEPD2: https://github.com/ZinggJM/GxEPD2
- Seeed Plus variant: vendored at `firmware/variants/Seeed_XIAO_nRF52840_Plus`
- nRF52840 PS reference: SAADC chapter, PSEL registers, GPIO PIN_CNF
- mbed OS Semaphore: https://os.mbed.com/docs/mbed-os/v6.16/apis/semaphore.html
- Datasheets: not committed (proprietary); download links in
  `docs/hardware/datasheets/README.md`

## 43. Baseline variance across font glyphs (verified 2026-08-24)

### The problem

Some TrueType fonts (notably Chango) report different `yOffset` values
for different digit glyphs. In Adafruit GFX, ALL characters rendered
by `print()` share a single baseline — the y-coordinate from `setCursor()`.
Each glyph's `yOffset` is the distance from that baseline to the top of
its bitmap. When glyphs have different yOffsets, they sit at different
heights on the shared baseline.

Chango at 82pt had a 2px spread:

```
'0': yOff=-84   '1': yOff=-82   '2': yOff=-82   '3': yOff=-84
'4': yOff=-82   '5': yOff=-84   '6': yOff=-84   '7': yOff=-83
'8': yOff=-84   '9': yOff=-84
```

'1', '2', and '4' sat 2 pixels lower than the others. The colon at
yOff=-83 was 1px off and its midpoint didn't align with the digits'.

This was not caught by the original FONT_GENERATION.md guide because:
1. The colon alignment pitfall was documented, but digit-to-digit
   variance was not
2. The `center` command only showed averages, not per-glyph values
3. After the xAdvance patch to fix horizontal overflow, nobody visually
   checked the sprite sheet for baseline consistency

### The fix

Manually normalised yOffsets in `FontChango82.h`:
- All 10 digits → -84 (the mode value)
- Colon → -82 (centered: digit midpoint -54.5, colon midpoint -54.5)

### Tooling: prevent recurrence

Three additions to `font_tool.py`:

1. **`generate` now checks TTF metrics** for per-digit yOffset variance
   before the header is even written. Warns with a per-glyph listing
   and the fix command.

2. **`center` now reports per-glyph yOffsets** with a `← differs` marker
   when any digit deviates from the mode value.

3. **`fix <header.h>` command** reads an existing GFXfont header,
   normalises all digit yOffsets to the mode value, then adjusts the
   colon's yOffset so its visual midpoint matches the digits'. One
   command — no manual arithmetic.

FONT_GENERATION.md updated with the fix step in the quickest path and
a "Common gotcha: baseline variance across digits" section.

## 18. Sprite sheet cell sizing for large fonts (verified 2026-08-24)

### The problem

`icon_tool.py sprite` had hardcoded cell dimensions: `cell_h=24` and
`icon_area_w=60`. These were chosen for 14-20pt Material Icons. When
rendering an 82pt time font, each glyph was ~62×71 px — far larger than
the cells. Every glyph overflowed into the next row, making the sprite
sheet useless for visual review.

The root cause was the tool not adapting to its input. The sprite command
was designed for status icons and nobody had ever fed it a display font.

### The fix

`cmd_sprite` now computes `max_ink_w` and `max_ink_h` from the actual
rendered glyphs and sizes cells dynamically. Glyphs are centered both
horizontally (within the widest glyph's space) and vertically (within
the tallest glyph's space). Works identically for 20pt Material Icons
and 82pt display fonts.

## 19. BLE MAC address: BLE.address() works, HCI.localAddr does not on Cordio (verified 2026-08-25)

### The problem

`HCI.localAddr` is never populated on the mbed Cordio BLE stack. Every byte
stays `0x00` because the field is only written when `readBdAddr()` is called,
and the Cordio stack doesn't call it during init.

### The fix

Use `BLE.address()` which sends the HCI `READ_BD_ADDR` command and returns the
real MAC as a String. The method internally calls `readBdAddr()` and formats
the result:

```cpp
String mac = BLE.address();
// Returns "f3:d2:b5:57:d1:1f" etc.
```

### Timing trap

The first `BLE.address()` call before `BLE.begin()` returns zeros. If you
display the MAC on a screen drawn before BLE init, the user sees all zeros.
Draw the sync screen, then call `BLE.begin()`, then redraw:

```cpp
drawClockFace();         // initial "Syncing..." — MAC may be zeros
BLE.begin();
drawClockFace();         // redraw after BLE init — MAC is valid now
```

## 20. xAdvance squish reveals hidden horizontal headroom (verified 2026-08-25)

### The problem

The font comparison tables in `FONT_GENERATION.md` quoted pre-squish widths
(e.g. "82pt widest is `10:00` at 293px"). After the Phase 5 fix applied a
uniform −8px squish to xAdvance, the actual rendered width was 275px — 18px
narrower than documented. The docs had been lying about the true ceiling since
the squish was applied.

### The fix

Update the docs with the real, squished widths. The sequence matters:

1. Generate the font header at the target size via `gfxfont_gen.py`
2. Edit the glyph table to reduce xAdvance (typically `xAdvance = width − 8`
   for Chango at 82–88pt)
3. Run `font_tool.py fix` to normalise yOffsets + centre colon
4. Run `font_tool.py center` to get the real rendered width (it accounts for
   xAdvance, not raw bbox width)
5. Update FONT_GENERATION.md with the actual numbers

### The ceiling: 88pt

With the −8px squish, 88pt is the clean ceiling for 12-hour display on a
296px panel. The widest string `10:44` fits at 290px (6px total margin).
Every 12-hour time string fits with zero clipping. At 90pt, even `10:44`
overflows.

## 21. Power budget model — the 1–6 month range (verified 2026-08-25)

The project has had an open question since Phase 3: "does a minute-resolution
clock fit inside a 500 mAh budget for three months?" A full six-term model
was written to `docs/research/power-budget-analysis.md`.

### Key finding

The gap between the plausible current range (4–40 mA refresh, 3–50 µA idle)
maps to 37–335 days of battery life. The 3-month goal is NOT guaranteed at
1-minute refresh. The single highest-leverage change is slower refresh (5–10
minutes), which clears 3 months across the whole range.

### Methodology

Run the model at the end of the analysis document:

```python
python firmware/tools/power_budget.py
```

It prints the daily energy breakdown and sensitivity matrix. All timing
values are measured; all current values are parameterised and flagged as
assumed.

### What to measure

Three measurements collapse the 37–335 day range to a single number:

1. Full-board idle current (WFE, panel powered)
2. Average current during one partial refresh (880ms window)
3. SSD1680 idle/standby current with the rail on

---

## 22. Host unit-testing the firmware without touching main.cpp (verified 2026-08-26)

A native (x86) test harness now exists under `firmware/test/`. It compiles the
**unmodified** `main.cpp` against mock headers and runs the state machine, time
math, battery logic and display drawing on the host — 98% line coverage, with
real PNG renders of the ePaper output. The key non-obvious lessons:

### Reaching `static` state without editing core code

`main.cpp` keeps 17 file-scoped `static` globals and many `static` functions.
The trick is that each test `.cpp` does `#include "main.cpp"` (via
`clock_harness.h`), making every static reachable in that translation unit.
One test executable per test file keeps the statics isolated per binary (no ODR
clashes, no cross-test interference). gcovr merges the per-binary `.gcda` data
and, thanks to `#line` markers, attributes the lines back to `src/main.cpp`.

### Host tests must define `ECLOCK_CORE_MBED` — the opposite of what you'd guess

`main.cpp` lines 7–11 `#error` unless `ECLOCK_CORE_MBED` is defined. So host
tests define the **mbed** branch and mock `mbed.h` + `ArduinoBLE.h` — not the
Adafruit core, which looks "more portable" but isn't what compiles.

### `ARDUINO=100` must be a compile flag, not a mock header

`Adafruit_GFX.h` evaluates `#if ARDUINO >= 100` at the very top — *before* it
includes `Arduino.h`. Defining `ARDUINO` inside `Arduino.h` is too late. It must
be `-DARDUINO=100` on the compiler command line (CMake `add_compile_definitions`).

### Adafruit_GFX.cpp needs more than a pgmspace stub

On x86 the library defines its own `pgm_read_*` fallbacks (no `pgmspace.h`
needed), but it still pulls in, via `Arduino.h`: the `radians()`/`degrees()`/
`sq()` macros and `sin`/`cos` (`math.h`). It also includes
`<Adafruit_I2CDevice.h>` and `<Adafruit_SPIDevice.h>` but never uses their
types — empty stubs suffice. And `main.cpp` calls `display.print(F("…"))`, so
the `Print` mock needs a `print(const __FlashStringHelper*)` overload.

### GFXcanvas1 is a ready-made ePaper fake — reuse it, don't reimplement

The fake `GxEPD2_BW` derives from Adafruit's own `GFXcanvas1`, which rasterizes
the real `FontChango88.h`/FreeSans fonts and `material_icons.h` bitmaps
byte-for-byte. Its bit order is MSB-first with **bit set = white**, which
matches both Pillow `'1'`-mode and the SSD1680 (1 = white). So a PNG dump is a
trivial bit-expansion to 255/0 grayscale — no masking inversion, no rotation.

### The panel is 128×296 portrait; firmware draws 296×128 logical

`GxEPD2_290_T94_V2` is `WIDTH=128, HEIGHT=296`, driven in landscape via
`setRotation(1)`. The firmware's coordinates already assume the post-rotation
296×128 space, so the fake canvas is `GFXcanvas1(296, 128)` with rotation left
at 0 (no transform) — the fake's `setRotation()` is a no-op.

### BLE `written()` has a trap: two `writeValue` overloads

In the real ArduinoBLE, the public `writeValue(buf, len)` (used by `setup()` to
initialise) does **not** set the written flag; only the remote-write path
(`BLELocalCharacteristic::writeValue(device, …)`, what Home Assistant triggers
over the air) sets it. A mock that sets the flag on any `writeValue` would make
`setup()` trigger a phantom time-sync on the first `loop()`. The mock models the
split: a `test_simulate_remote_write()` helper is the over-the-air path.

### board_pins.h compiles register writes even when you don't call them

`eclock_release_twim_pins()` (guarded by `ECLOCK_CORE_MBED && NRF_TWIM0`) writes
`NRF_TWIM0/1->ENABLE` and `NRF_TWI0/1->ENABLE`, and `setup()` actually calls it.
Defining `NRF_TWIM0` (as a real writable struct, not a dangling address) makes
that branch compile *and run* on host. The nRF52 fakes are `static` structs, so
the writes are safe no-ops.

### Toolchain on this Windows box

No `g++`/`gcc`/`make` was present (only MSVC Build Tools). MinGW-w64 was
installed via `winget install BrechtSanders.WinLibs.POSIX.UCRT`, which ships
`gcc`/`g++`/`gcov`. Coverage needs GCC (MSVC coverage is OpenCppCoverage, far
more painful). `gcovr` is installed via `uv tool install gcovr`.

### Coverage ceiling is 98%, not 100% — and that's correct

The only uncovered lines are the `BLE.begin()`-failure infinite loop
(`while(1)` blink, lines 512–515), which cannot be exercised without hanging
the runner. Everything else — every state transition, both battery branches
(USB bolt and percentage), the defensive `STATE_SLEEPING`/`STATE_NO_TIME` loop
cases, the mid-minute partial tick, the DST-immune sleep — is covered.

### Latent assumption worth knowing (not a bug to fix)

`updateTimeStruct()` does `g_hour = (local_time/3600) % 24` where `local_time =
g_epoch + g_tz_offset`. If `local_time` were negative, `%` returns a negative
value that wraps into `uint8_t` (e.g. hour 251). This is unreachable in practice
(any real epoch is ≫ any timezone offset) — document, don't "fix".

### How to run

```bash
# toolchain on PATH (MinGW-w64), then:
cmake -S firmware/test -B firmware/test/build -G Ninja
cmake --build firmware/test/build
ctest --test-dir firmware/test/build --output-on-failure
cmake --build firmware/test/build --target coverage   # HTML in build/output/coverage/
# PNG renders land in firmware/test/output/
```

---

## 23. The shipped firmware builds ONLY on the `mbed` core — and battery spacing (verified 2026-08-27)

### The `adafruit` env no longer compiles — use `-e mbed`

`main.cpp` opens with a hard guard:

```cpp
#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif
```

So `pio run` (the default `adafruit` env) fails immediately with the `#error`, plus a
cascade of `BLEService`/`BLECharacteristic`/`rtos::Semaphore` "does not name a type"
errors. The `adafruit` env is now **dead** for the current firmware. **Always pass
`-e mbed`**:

```bash
pio run -e mbed
pio run -e mbed -t upload --upload-port <bootloader port>
```

This is worth pinning. `docs/SETUP.md` and the README now correctly lead with
`-e mbed`, and `firmware/tools/reset_to_bootloader.py` prints the `-e mbed` upload
hint (it previously said `-e adafruit`, which is wrong for the current firmware). The
host test harness sets `ECLOCK_CORE_MBED` for the same reason (see §22).

### PlatformIO's `platforms.lock` is outside the workspace — a file-sandbox gotcha

`pio run` fails with `PermissionError: [Errno 13] ... '.platformio\\platforms.lock'`
when the invoking process cannot write to `~/.platformio/`. The platform/packages are
already installed, but PlatformIO still tries to *acquire* the lock file on every
build, so a non-writable home dir breaks even a no-op incremental build. Under a file
sandbox this looks like a build error but isn't a code problem — grant the process
write access to `~/.platformio` (and `~/.platformio/packages`) and it succeeds. If the
build is later reported "mysteriously hung", the EPERM-on-piped-stdout quirk can make
`cmake --build` return with empty output while the work actually completes — check the
target binary's timestamp rather than trusting the pipe.

### Firmware visual tweaks can be reviewed without flashing — use the PNG dump

The host harness (`firmware/test/`) renders `drawClockFace()` to a real 296×128 PNG in
`firmware/test/output/`. For a layout tweak, regenerate the render and inspect the PNG
**before** uploading to hardware. The fixture compiles `main.cpp` directly, so a change
takes effect the moment `test_display` is rebuilt:

```bash
cmake --build firmware/test/build --target test_display
./firmware/test/build/test_display.exe    # writes firmware/test/output/*.png
```

### Battery indicator spacing (the change that prompted this lesson)

The clock face draws `battery icon + percentage` right-aligned. Originally the gap
between the icon and the `N%` text was a hardcoded **2 px** (`text_x = width - 2 -
icon_battery_w - 2 - bw`) — visually the text touched the icon. The user found it too
close; the fix widened the gap to **6 px**. Layout constants like this are cheap to
tune and easy to eyeball via the PNG dump, so "nudge the icon/text apart" is a one-line
main.cpp change plus a re-render — no firmware reflash needed to judge it.

### Deploy-from-CLI recap (mbed core)

```bash
cd firmware
pio run -e mbed                              # build the binary
python tools/reset_to_bootloader.py          # 1200-baud touch -> bootloader (prints port)
pio run -e mbed -t upload --upload-port COM6 # use the port it printed ("Device programmed.")
```

After upload the board re-enumerates as the application (PID `0x8045`) on a separate
COM port — verify with `python -c "import serial.tools.list_ports as l; [print(p.device,
hex(p.pid), p.description) for p in l.comports()]"`.

---

## 24. Low-battery warning, and two traps it exposed (verified 2026-08-27)

Added a real low-battery warning: **≤20%** swaps the running-face battery icon to the
empty-battery glyph (still showing the %), and **≤5%** draws a final "LOW BATTERY /
Charge me now" screen once, stops the radio, and locks the display so nothing redraws
over it. Two traps surfaced while wiring this up:

### Signed sentinel vs threshold: `-1 <= 5` is true

`getBatteryPercent()` returns **`-1`** as a sentinel when USB VBUS is detected ("not a
percentage"). The loop's critical-battery check did `getBatteryPercent() <=
CRITICAL_BATTERY_PCT` with the constant **5**. In C, `-1 <= 5` is **true**, so a
USB-powered clock drew the LOW BATTERY screen on boot. The display-side render was
already safe (it tests `batPct < 0` first and draws the bolt icon), but the loop's
one-line check had no guard.

**Fix:** never compare a sentinel against a threshold without first rejecting the
sentinel — `batPct >= 0 && batPct <= CRITICAL_BATTERY_PCT`. The regression test
asserts `getBatteryPercent() == -1` on USB *and* that the low-battery lock is not
engaged. This is the general lesson: an "unreadable / special" sentinel value must be
filtered out before any numeric comparison, or the sign/magnitude of the sentinel can
accidentally satisfy the condition.

### A latent malformed icon exposed by first use

`icon_battery_empty` (Material symbol U+F30D) in `material_icons.h` was an **open-top**
battery outline — only side rails and a bottom bar, no top edge, no nub. No code used
it before, so the bad data sat silently. The moment the low-battery warning started
drawing it, the "⊔" shape became obvious. It was regenerated as a closed rectangular
outline matching the full battery's silhouette. **Lesson: an asset that is generated
but never exercised can carry garbage; "first use" is where such latent defects
surface, so visually review any newly-used icon/sprite on the first render.**

### Run the fixture from the right working directory (stale-render trap)

The display test writes `output/*.png` **relative to the current working directory**
(the CMake `add_test` sets `WORKING_DIRECTORY` to `firmware/test`, but invoking
`test_display.exe` directly does not). Running it from `firmware/` or the repo root
dumped PNGs into a stray `output/` folder at that location — and, worse, my visual
"verification" read a **stale** `firmware/test/output/*.png` that the run had not
actually regenerated (same byte-identical hashes). **Always run the test binary from
`firmware/test/`** (or via `ctest`), and confirm the PNG's mtime moved before trusting
a render.

### Deploy-to-review loop for a firmware change

A visual/logic change is best reviewed without a hardware round-trip: regenerate the
PNG via the fixture first, then flash once you're happy, then confirm the board
re-enumerates on its application COM port.

---

## 25. Release artifacts (UF2), firmware version, and CI guardrails (verified 2026-08-27)

### The nRF52 app does not start at 0x00000

The mbed `.hex` for this board begins at **0x27000**, not 0x00000 — space below that is
the soft device / bootloader. This matters for any raw-flash artifact (UF2, `--base`)
and for interpreting a hex dump: `firmware/.../conf/mbed_app.json` sets
`target.mbed_app_start = 0x27000`, and the first data record of `firmware.hex` is at
`(segment 0x2000):0x7000` = 0x27000. Get the base from the linker/app-start config, not
a guess.

### UF2 from the .hex: base + family ID

The canonical `uf2conv.py` (microsoft/uf2) converts a `.hex` to a `.uf2` for
drag-and-drop flashing. It needs two numbers:
  - **`-b 0x27000`** — the app base (see above).
  - **`-f 0xada52840`** — the nRF52840 family ID (this is the board's family, not a
    device-unique value).
Command used in the release workflow:
  `python uf2conv.py <firmware.hex> -o <firmware.uf2> -f 0xada52840 -b 0x27000 -c`.

### The release UF2 did NOT flash — root-caused and fixed (2026-08-27)

The `v0.1.0` release `.uf2` (734 KB, built with `-b 0x27000 -f 0xada52840`) **did not
boot**. Inspecting the block headers showed the cause: **uf2conv.py `-c` wrote the
family ID (`0xada52840`) into the `fileSize` header slot and omitted the magic-end
fields** — so the block header was malformed and the bootloader rejected it. It was
non-empty and "looked" like a UF2 (magicStart present) but the header fields were in
the wrong places. This is the classic artifact that **only "doesn't boot" rather than
failing loudly** — a linter/parse would pass it.

**Fix:** `firmware/tools/hex_to_uf2.py` — a self-contained Intel HEX → UF2 converter that
writes the header correctly (`fileSize` = real image size, `familyID` in its own slot,
valid `magicEnd0/1`). Verified: 1436 contiguous blocks, all magic valid, at base
`0x27000` / family `0xada52840`. The release workflow now uses this instead of
downloading `uf2conv.py`.

**Takeaways:**
- **Inspect a generated flash artifact's header, not just its size.** A valid
  `magicStart` does not mean the rest of the block header is right. Parse the `fileSize`
  and `magicEnd` fields.
- **Do not trust a generated artifact without a hardware smoke test.** "Built + non-empty
  + valid magic" is not "boots". The release pipeline validates *build*, not *flashing*.
- **Prefer a small, understood, self-contained converter** over downloading a generic
  tool with modes you don't control. `uf2conv.py -c` is the trap; writing 80 lines you
  can verify beats guessing at a tool's semantics.

The reliable path on this machine is serial DFU of the `.hex` (the bootloader's
XIAO-BOOT drive does not remount dependably). The `.uf2` is a convenience that has not
yet been proven to boot.

### Firmware version: single source, CI-injected

We show the build's version at the bottom left of the syncing screen. The clean pattern:
a `firmware/include/clock_version.h` with a plain `#define ECLOCK_VERSION "0.1.0"`
guarded by `#ifndef` so a build can override it with `-DECLOCK_VERSION="..."`. The CI
release workflow regenerates that header from the release tag *before* `pio run`, so the
number on screen always matches the release. Keep it a display-only string — the clock
never branches on it. Thread it into the view layer the same way as any payload: add a
`version` field to `ClockView`, render it only if non-empty.

### Pre-commit + gitleaks as the change gate

`gitleaks` runs as a pre-commit hook (and again in CI) to block any credential/token/
private key. It needs a `.gitleaks.toml` allowlist for deliberately-committed binary/`
third-party content (the vendored mbed `.a` libs, `firmware/test/third_party`) —
compiled blobs trip the generic lookalike heuristics. Hygiene hooks (EOF-fixer,
trailing-whitespace) auto-fix on first run, which touches many files at once; run
`pre-commit run --all-files` once, commit the style churn, then it stops recurring.

### YAML `on:` key gotcha (GitHub Actions vs PyYAML)

GitHub Actions' `on:` (workflow trigger) is parsed as the boolean `true` by PyYAML's
YAML 1.1, so `yaml.safe_load(...)["on"]` raises `KeyError` — a false alarm when
validating a workflow locally. GitHub uses its own parser that handles `on` correctly.
To validate, check `jobs`/step structure instead of `["on"]`, and never assign a second
step the same `id` in a job (duplicate step ids are invalid).

## 26. UF2 drag-and-drop requires 8.3 FAT16 compliant filename (verified 2026-08-29)

The bootloader's tiny simulated FAT16 mass storage drive does not fully implement Long File Names (LFN). Copying a UF2 file with a long name (like eClock-v0.1.0.uf2) crashes the bootloader mid-transfer because Windows attempts to write VFAT LFN directory entries. This yields a 'device which does not exist' error.

**Fix:** Rename the UF2 file to an 8.3-compliant name (e.g., ECLOCK.UF2) before copying, or use the GitHub Release workflow which now automatically produces an 8.3 compliant filename.

---

## 27. Keep docs, screenshots and firmware README in sync (verified 2026-08-29)

Docs drift silently. This session surfaced three separate sync gaps; each cost a
regeneration/redo pass. Capture all three as a standing checklist before committing
any doc change.

### 27.1 Screenshots go stale without any code change
`docs/screenshots/*.png` are real renders from the host harness. A change to a renderer,
font, icon, **version string**, or layout makes them stale — even though the build still
passes. Regenerate with the harness (see `docs/lessons/TEMPLATE.md` "Docs maintenance
checklist") **in the same commit** as the doc change, and verify with `md5sum`.

### 27.2 There are *two* different "sleep" screens — don't swap them
The eClock has two sleep renders and they are NOT interchangeable:
- `drawSleepingScreen()` → **Zzz only**. Used by the *defensive* `STATE_SLEEPING` case in
  `drawClockFace()`. Harness output `output/sleeping.png`.
- `drawSleepIcon()` → **Zzz + "Sleeping"**. The *real overnight* screen left on the panel
  before the rail is cut (`enterSleepMode()`). Harness output `output/sleep_icon.png`.

`docs/screenshots/sleeping.png` must be the **overnight** screen (`sleep_icon.png`),
because `docs/USER_GUIDE.md` describes it as "Zzz" with *Sleeping* beneath it. A prior
commit wrongly used the Zzz-only render; caught by reading the USER_GUIDE text back
against the image. **Lesson: always reconcile the image against what the prose promises.**

### 27.3 `firmware/README.md` was a Phase-1 stub
It read "Nothing here yet" and "the core has not been chosen yet" — both false for a
long time (the firmware is complete and mbed-only). The top-level `README.md` was kept
current but the per-directory one rotted. **Lesson: when you touch a README, sweep the
whole repo for other stale READMEs/stubs, not just the file in front of you.**

---

## 28. Windows: MinGW runtime DLLs break ctest from a clean shell (verified 2026-08-29)

The host test executables that link the **dynamic** GCC runtime fail on Windows with
`STATUS_ENTRYPOINT_NOT_FOUND` (`exit code 0xc0000139`) when `ctest` launches them
without the MinGW `bin` dir on `PATH`:

```
3/9 tests failed: test_clock_display, test_smoke, test_display  (0xc0000139)
```

**Root cause:** those suites link `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and
`libwinpthread-1.dll` (imported by MinGW, not the UCRT/OS). The other six suites don't
link them, so they always pass — which hides the problem. CI never hits it because it
runs on Linux glibc, which has no DLL-resolution layer.

**The fix (implemented in `firmware/test/CMakeLists.txt`):** resolve the MinGW bin dir
from `CMAKE_CXX_COMPILER` (robust, not hardcoded) and copy the three DLLs next to each
test executable via a `POST_BUILD` `copy_if_different` step. Windows then loads them
from the exe's own directory, so `ctest` works from a clean shell with no PATH setup.

**Verify:** run ctest with MinGW **removed** from PATH — all 9 suites must pass. (The
PASS times jump from ~0.03s to ~0.2s: the DLLs are genuinely loaded from disk next to
the exe, not resolved via a PATH hit.)

**Licensing (checked):** the DLLs are **GPLv3 + GCC Runtime Library Exception**, which
explicitly permits distributing these unmodified runtime libraries with a program. They
are only copied into the local `firmware/test/build/` dir (gitignored) — never
committed or redistributed.

**Two trigger patterns worth knowing:** (a) a suite that links the dynamic runtime will
fail only on the *host* machine, not CI; (b) `0xc0000139` is "entry point not found" —
a missing/version-mismatched DLL, **not** a logic bug. When you see it, check the
executable's imported DLLs (`objdump -p`) before touching code.

---

## 29. Rendering a Mermaid diagram for the docs (verified 2026-08-29)

`docs/design/state-machine.png` is rendered from `state-machine.mmd` with
**@mermaid-js/mermaid-cli** (`mmdc`). The tooling is machine-local, not committed.

**Install (once, globally so it's usable from anywhere):**
```bash
npm install -g @mermaid-js/mermaid-cli   # provides the `mmdc` command
npx puppeteer browsers install chrome     # headless Chromium (cached by puppeteer)
```

**Render:**
```bash
mmdc -i docs/design/state-machine.mmd -o docs/design/state-machine.png -b white
```
Note `-b white` (else the PNG background is transparent). Commit **both** the `.mmd`
and the `.png` together — the `.mmd` is the editable source, the `.png` is the artifact
the doc embeds.

**Gotchas (each cost a retry):**
- **Mermaid parser errors are off-by-one.** A parse error pointing at line N is often a
  *previous* line leaving the parser mid-construct. Isolate by shortening, or simplify.
  The `flowchart` parser is pickier than `stateDiagram-v2`.
- **Quotes for special chars.** Parentheses/`!=`/`[]` inside node labels need quoting,
  else Mermaid misreads them as node shapes. Prefer `node["label"]` and `"edge label"`.
- **The `%%{init: ...}%%` block must come before `flowchart`/`stateDiagram`**, and be on
  its own lines adjacent to the `%%` comment markers.
- **`class A,B,C state;` (comma list) breaks the `flowchart` parser** in some versions.
  Per-node `style X fill:...` is more reliable.
- **Edge routing:** `flowchart` defaults to curves. `%%{init: {"flowchart": {"curve":
  "step"}}}%%` gives orthogonal (right-angle) elbows, but with many long edges that
  produced a messy, crossing layout — **the default curved `flowchart` looked best** for
  this diagram. `stateDiagram-v2` gives true UML start/stop (filled dot + bullseye) but
  keeps curved edges. Choose per-diagram; this repo settled on the curved `flowchart`.
- **Path gotcha (git-bash/Windows):** `mmdc`/node can't see bash `/tmp`; pass **native**
  Windows paths like `C:/Users/.../state-machine.mmd`, not `/tmp/...`.

**Git:** node_modules and the puppeteer cache are ignored in `.gitignore`
(`node_modules/`, `.cache/puppeteer/`), so installing the tool locally never dirties the
repo. `docs/design/package*.json` is ignored too.

---

## 30. A failed re-sync must back off a full hour — and how TDD caught it (verified 2026-08-29)

**Symptom (observed on hardware, bedside clock):** the sync icon kept re-appearing
every minute. The state machine was re-entering `RESYNCING` far more often than the
intended once/hour.

**Root cause:** `g_last_sync_millis` (the timestamp the hourly re-sync gate compares
against) was updated **only on a successful BLE time write**. On a **failed** re-sync
the code went back to `RUNNING` and set `g_last_sync_failed = true`, but never advanced
that timestamp. So the gate `if (now - g_last_sync_millis >= SYNC_INTERVAL)` stayed
true, and the clock re-entered `RESYNCING` on the *next* `loop()` iteration — a tight
retry loop that kept the radio advertising and drained the battery.

**Why the tests didn't catch it:** the existing `RunningResyncIntervalTriggersResync`
test only asserted that the clock *enters* RESYNCING after a full interval. The
`ResyncingTimeoutWithEpochGoesRunningFailed` test asserted the timeout returns to
RUNNING. **No test covered the very next `loop()` iteration after a failed attempt** —
the exact place the loop happened. (A great example of a coverage *gap*, not a code
*defect*, letting a real bug through.)

**The fix (TDD):** write the regression test first (`FailedResyncBacksOffFullHour...`),
watch it fail, then add one line in the failed-resync path:

```cpp
g_state = STATE_RUNNING;
g_last_sync_failed = true;
g_last_sync_millis = millis();   // <-- push the gate forward: back off a full hour
g_needs_display_update = true;
```

The clock now backs off a full `SYNC_INTERVAL` (1 h) after a failure. A button press
still forces an immediate manual re-sync (separate path, unchanged). The battery is
protected — no more radio hammering.

**Design principle worth naming:** "don't hammer a resource that just failed." When a
time-based retry depends on a *last-success* timestamp, make sure a *failure* also
moves that timestamp forward (or has its own backoff), otherwise the "it's been long
enough" condition remains satisfied and you get a hot loop. A dedicated regression
test for the "the tick *after* the failure" is what closes the gap.

**TDD pattern here:** for a bug that only manifests a *full cycle after* a state change,
write the test to run the full scenario (sync → interval → resync → timeout → *one more
loop*) rather than asserting a single transition. That's what made the failure
reproducible and the fix verifiable.

---

## 31. Bottom-message feature & the reworked clock-face layout (verified 2026-08-29)

Added an HA-pushed **bottom message** (e.g. a holiday greeting or weekly reminder) and
reworked `drawRunningFace` to the settled layout in `docs/research/message-feature-design.md`.

### The wire contract (shared HA <-> firmware)
- **New additive characteristic** `c0dec10c-2a2c-4a20-8c10-000000000000` — never touch
  the 8-byte `0x2A2B` time sync. Old firmware ignores it; HA writes time first, then the
  message best-effort (its write failure must not block the time sync).
- **Value format:** fixed-width **30-byte NUL-padded ASCII**. HA truncates to 30 chars;
  the firmware reads up to 30 bytes, stops at the first NUL, NUL-terminates. All-NUL
  clears the line. This makes the buffer length trivially safe at both ends.
- **Custom 128-bit UUID** was chosen (not a 16-bit sibling of 0x2A2B) to avoid any clash
  with a Bluetooth SIG assignment, and because a 16-bit char in the same service would
  be ambiguous on the wire.

### Config (HA side) — natural `day:` rules, only two keys allowed
```yaml
epaper_clock:
  messages:
    - message: "God is good"            # default (no rule) — lowest precedence
    - message: "Thank God it's Friday!" # weekly
      day: Fri
    - message: "Merry Christmas"        # annual
      day: 25 Dec
    - message: "Happy Birthday Sam"     # one-off (highest among date rules)
      day: 14 Mar 2026
```
- **Precedence:** one-off/annual matching today > weekday > default; within each group
  LAST list entry wins (a one-off can override an annual on the same date).
- `day` is case-insensitive; weekday/month names supported. **Only `message` and `day`
  keys are accepted.** Entries with a missing/blank message, an unknown key, or an
  unparseable `day` are reported as errors in the HA log and skipped — never silently
  turned into a default.

### Layout lessons
- The top row is now a **single right-aligned group**:
  `[date]  [sync time?][sync icon]<2px>[%][battery icon]` — not independently-positioned
  elements. The sync icon top edge is flush at y=0; the other icons/text are aligned to
  it. The old bottom-left sync status moved up into the group.
- **Text/icon vertical alignment is by VISIBLE INK, not the glyph bounding box.** The %,
  battery, and sync glyphs have different heights and blank padding rows, so aligning
  nominal centres looked wrong; aligning visible-ink centres (measured) is what looked
  right. Same for the message left-edge: `getTextBounds` box != ink, so seat spacers by
  measured ink.
- **The bottom message is clamped at render time** so it never reaches AM/PM (measured:
  30 chars leaves a ≥2-char margin). HA also truncates to 30 server-side — belt and
  braces.

### Testing
- The pure selection logic is unit-tested (pytest) in `integrations/homeassistant/tests/`
  — no HA needed. `parse_messages` returns `(messages, errors)` so the HA layer logs
  invalid lines.
- The firmware message write + render are covered by host tests (store/clear/clamp, and
  PNG renders). The BLE mock was extended to hold multiple characteristics, and the
  harness gained `simulateBleMessageWrite()` + `message()`.

**Key lesson:** when adding a paired HA + firmware feature, define the wire contract
first (UUID, value format, NUL-padding, truncation) and keep it **additive** so older
devices are unaffected. Render-side, measure by *ink* not by *box* when placing text
next to icons.

### Hard-won rendering lessons from the review cycle
These were found by rendering and pixel-measuring the top-right group, then iterating
until it looked right. Each was a real bug, not a style preference.

- **A sync glyph's height varies, so DON'T derive the group's centre line from it.**
  `icon_synced`/`icon_failed` are 17px tall (visible centre 8.0 when flush), but
  `icon_syncing` is only 13px tall (centre 6.0). Computing the shared centre line from
  the *active* sync icon made the whole group jump up ~2px whenever the clock was
  syncing. Fix: use a **fixed** centre line (8.0) for every state, and draw the sync
  icon top-edge flush; its shorter height then just means it doesn't reach as far down,
  without moving the group.

- **Icons with blank padding rows break bounding-box alignment.** The battery bitmap is
  15px tall but its ink only occupies rows 0–9 (5 blank rows at the bottom), so its
  bounding-box centre (7.5) ≠ its visible-ink centre (4.5). Aligning by box put the
  battery ~2.5px too high. Fix: a `visibleBitmapCentreY()` helper that scans for rows
  with ink and centres on those. Same idea for horizontal: `visibleBitmapInkLeft()` to
  seat neighbours against an icon's actual ink left edge (the % and bolt glyphs have
  left sidebearing / blank padding).

- **Seat text by its INK, not its bounding box.** `getTextBounds` returns the box plus an
  ink offset (`bx`); the ink spans `[box_x+bx, box_x+bx+bw]`. Seating a neighbour
  against the *box* produced a visible gap (the % glyph has ~5px sidebearing → a 7px
  loose gap instead of the intended ~2px). Seat against `box_x + bx`.

- **The USB/no-% case must seat against its own icon, not a text position.** When there
  is no `%` (USB/bolt), the group is `[sync icon][bolt]`. Seating the sync icon against
  a stale "text ink left" position left a huge whitespace. Fix: branch so each power
  case (battery+%, bolt) sets the sync icon's anchor to that case's own ink left edge.

- **Empty = all-NUL payload, and it's the "no message" signal.** The firmware treats the
  first NUL as end-of-string; HA writes an all-NUL 30-byte payload when no message
  applies so the clock clears the line. A plain empty/blank string must not be sent as a
  zero-length write — always the fixed-width NUL-padded frame.

> **Note on earlier layout lessons:** §17/§18 (the old "status line at the bottom
> left / icons at the bottom left" descriptions, verified 2026-08-23/24) describe the
> PREVIOUS layout and are superseded by this rework — sync status is now a single
> right-aligned group at the top, and the bottom row holds the message.

---

## 32. Screenshots went stale because some test binaries weren't rebuilt — and the fix (verified 2026-08-29)

The `docs/screenshots/` images are produced by **two different host-test executables**:
- `test_display.exe` — drives the **main.cpp harness** and dumps the "real" screens
  (`running`, `syncing`, `no_time`, `sleeping`, `running_usb`, `running_low_battery`,
  `low_battery`).
- `test_clock_display.exe` — drives the **pure `clock_display` module** and dumps the
  `cd_*` renders (`cd_running_message`, the `cd_state_*` matrix pieces).

When a render bug was fixed and re-verified, I rebuilt **`test_clock_display`** (which
produced the state matrix I was checking) and copied *its* renders — but I had not rebuilt
**`test_display`**, so the "real" screenshots I copied were still from the OLD binary. The
result: `running_usb.png` showed the stale 24px sync-icon→bolt whitespace even though the
source code had the fix. The fix was in the source, verified in the matrix, but the
harness-driven PNG was stale.

**Root cause:** `cmake --build <dir> --target <one-exe>` only rebuilds/reruns that one
target. The PNGs live on disk in `output/` and are copied to `docs/` — so stale files
persist until *their* producer is rebuilt. There is no "the screenshots are the current
code" invalidation.

**The fix:** a one-shot script, `firmware/tools/regenerate_docs_screenshots.py`, that
rebuilds ALL targets, runs BOTH PNG-producing suites, applies the correct mapping
(including `sleep_icon.png -> sleeping.png` and `cd_running_message.png -> message.png`),
assembles the state-matrix composite, and verifies every docs-referenced image is present
and 296×128. Use it instead of manually rebuilding a single target + copying.

**Lessons:**
- If a docs image depends on a render, regenerate the **producer binary** (and rebuild it),
  not just the one you happen to be looking at. `--target <exe>` hides that.
- Prefer a single "regenerate all artifacts" entry point over ad-hoc copy steps, so the
  set can't drift.
- Two harness screens can share a name but differ: `output/sleeping.png` (defensive Zzz
  only) vs `output/sleep_icon.png` (overnight Zzz + "Sleeping"); the docs want the latter.

---

## 33. Vertical-stretching the time font, and realistic ePaper screenshots (verified 2026-08-29)

### The "go larger" trap (and the squish recap)
When asked to make the time larger, a naive exploration generated *raw* Chango fonts
(no spacing applied) and concluded the current font "overflows" / there's room to grow.
That was WRONG: the shipped `FontChango88.h` has the **−8px xAdvance squish**
(`xAdvance = width - 8`, see §20), so its real widest string `10:44` is **282px** — it
fits the 296px panel with +14px. Raw fonts measure ~332px and look like they overflow.
**Always measure the *in-tree* font (with its squish), not a freshly generated one.**

### The width ceiling is real: 88pt
Even squished, 88pt is the horizontal ceiling (§20): at 90pt `10:44` overflows. So you
**cannot** go bigger to fill vertical space — scaling the font widens the glyphs.

### Solution: vertical-only stretch (`--stretch-height`)
To fill more vertical space *without* overflowing width, scale each glyph's bitmap
vertically while keeping width and the squished xAdvance. Added an optional
`--stretch-height <scale>` to `gfxfont_gen.py` and threaded it through `font_tool.py
generate`. The shipped font is now 88pt with `--stretch-height 1.1` + the squish:

```
python tools/gfxfont_gen.py Chango.ttf 88 48 58 out.h --stretch-height 1.1
# squish xAdvance = width - 8, then:
python tools/font_tool.py fix    src/FontChango88.h   # normalise yOff + centre colon
python tools/font_tool.py center src/FontChango88.h   # real width/centring math
```
Result: digit ink 64px → **70px** (73px for the tall '0'), V-fill ~67% → ~76%, widest
string still 282px (`10:44`, +14px margin). Big-time ink now y=26–95 with an 8px gap
to the top row and 15px to the bottom. The digits read as slightly taller Chango.

> **Gotcha:** after post-editing the glyph table, run the real `fix` tool — it re-centred
> the colon yOffset (-97 → -95) that a hand-set uniform value got wrong. Don't hand-code
> metrics the tooling already computes. Also remove any duplicate trailing commas when
> rewriting a glyph table (`},,` breaks the build).

### Realistic ePaper screenshots (light-gray background)
The docs screenshots are grayscale PNGs from the host harness. `png_dump.cpp` mapped
bit=1 (white) to `255` (pure white). Real ePaper panels are off-white/warm; pure white
reads as "screen off/document." Changed the background to light gray (`0xD8` ~216),
keeping ink black (0). Now `docs/screenshots/*.png` look like the actual panel. The
`regenerate_docs_screenshots.py` state-matrix composite was updated to use the same
`0xD8` background so the collage is consistent.

**Lesson:** regenerate ALL docs screenshots from the (rebuilt) harness after any render
change — the `regenerate_docs_screenshots.py` script does this in one shot and now
produces the gray-background, stretched-font set.

---

## 34. Choosing a time font + the colon-baseline generator bug (verified 2026-08-29)

### v0.3.0: the big-time font is now Titan One 104pt (no squish, no stretch)

A 16-font comparison was measured against the real 296×128 geometry
(`docs/reference/candidate_fonts_comparison.png`). Results:
- **Titan One 104pt** — chosen. Width-constrained (like Chango), fits `10:44` at 283px
  (L5/R8 margin), ink ~82px fills the vertical band, no squish or vertical stretch
  needed. Reads clean and bold.
- **Chewy** — rejected. Its natural advance = glyph width (no built-in tracking), so the
  Chango `-8px squish` causes glyph overlap AND the colon (a 19px-wide glyph) collapses
  to ~11px advance and visually vanishes. With NO squish it works but its wide "4"s and
  playful style made it a weaker fit than Titan One.
- **Chango 88pt (squished + 1.1× stretch)** — superseded (was v0.2.0's font).

### The colon-baseline generator bug (this drove a real fix)

`gfxfont_gen.py` set every glyph's `yOffset = -bbox[3]`. That only bottom-aligns glyphs
that share the same `bbox[1]` (ink TOP). The colon's `bbox[1]` differs from the digits',
so a naive generator **top-aligns** the colon and it FLOATS above the baseline. In the
renders the colon sat ~19px high. Shipped Chango avoided this only because a prior manual
`font_tool fix` happened to bottom-align it within 5px.

**The fix (now in the generator):** bottom-align every glyph to the digits' baseline:
`baseline_bottom = mode_digit_yOffset + avg_digit_height`, then
`glyph.yOffset = baseline_bottom - glyph.height`. This makes the colon's ink bottom share
the digits' baseline (bottom delta → 0px). Confirmed against a PIL ground-truth render.
`--center-colon` remains as an opt-in for fonts that genuinely need a centred colon.

### Self-describing font headers
The generated header now carries its own metrics so callers don't hardcode them:
- `// Recipe: pt=… stretch=… squish=… yAdvance=…`
- `// Colon: NATIVE (honours typeface) yOffset=… h=…`
- `#define FONT_BASELINE <y>` — the setCursor baseline that vertically centres the digits.

`drawClockFace` reads `FONT_BASELINE` instead of a hardcoded 123/130. New options:
`--squish <px>` (default 0), `--center-colon`.

**Lessons:**
- The GFX glyph `yOffset` convention (glyph top = baseline + yOffset; bottom = top+height)
  means a shared yOffset does NOT bottom-align different-height glyphs. Bottom-align
  requires `yOffset = shared_baseline_bottom - glyph_height`.
- Measure colon alignment against a PIL ground-truth render (native TTF), not metric
  arithmetic — the sign/offset conventions are easy to misapply.
- Keep per-font "squish/stretch/colon" decisions in the generator options + header, never
  hand-edit per font. The visual-fit numbers (fill %, margins) in the docs are per-font;
  don't assume a squish recipe generalises (Chango needed -8px; Chewy needs 0).

---

## 35. Use CodeGraph first for source discovery, then verify its boundaries (verified 2026-08-29)

A repository summary was initially assembled by reading Git's file list and the main
prose documents even though this checkout already had an up-to-date CodeGraph index.
That missed the fastest route to the code's actual symbol and relationship structure.
The repo now has `AGENTS.md` guidance and a CodeGraph quick-start in `README.md` so the
index is checked before manually walking source.

### Start by checking availability and freshness

```bash
codegraph status .
```

For this checkout, CodeGraph v1.6.0 reported an up-to-date index containing 65 files,
940 nodes, and 2,254 edges. If the status says the index is stale, run
`codegraph sync .` before relying on it. If the command or index is unavailable, fall
back to direct source, docs, Git, and filesystem inspection rather than blocking.

### Broad natural-language context can be low-confidence

A broad `codegraph context` request for "architecture, runtime, tests, integrations and
tooling" matched generic words and returned battery-test functions as entry points.
CodeGraph explicitly labelled that result low-confidence. The reliable workflow was:

1. Use `codegraph files -p . --format grouped` to establish indexed structure.
2. Use `codegraph query -p . "<exact symbol>"` to locate entry points such as `loop`,
   `setup`, `async_setup`, and `async_perform_sync`.
3. Use `codegraph node -p . "<symbol>"` to read current source plus caller/callee trails.
4. Use a narrowly-worded `codegraph explore` query for one subsystem at a time.
5. Verify important claims against the returned source and relevant documentation.

**Lesson:** treat a low-confidence semantic match as a lead, not as a repository
summary. Exact symbols and focused subsystem queries produce much stronger evidence.

### The graph is source intelligence, not the whole repository

The index covers C/C++, Python, and selected YAML/configuration files. It does not
represent all Markdown documentation, screenshots, generated build artifacts, or every
tracked file. Therefore a complete summary still needs:

- `README.md`, `docs/STATUS.md`, and relevant design docs for intent, hardware evidence,
  current phase, and known gaps;
- Git for tracked-file counts, history, and working-tree state;
- filesystem/build inspection for generated outputs and local artifacts.

**Lesson:** CodeGraph should be the first tool for code architecture and relationships,
but it complements rather than replaces prose docs and repository-state tools.

### Confirm which CodeGraph implementation is installed

The `codegraph` executable in this environment is the npm package
`@colbymchenry/codegraph`, not similarly named CodeGraph projects. Before linking docs
or copying commands from the web, verify the installed package and CLI help:

```bash
codegraph version
codegraph --help
npm view @colbymchenry/codegraph repository.url
```

This prevents documenting a different product whose commands happen to have a similar
name.

## 36. Architectural Refactoring and SOLID Principles (verified 2026-09-04)

During a code review and refactoring pass, several structural improvements were made:
- **Display logic decoupling**: The display rendering functions were refactored into smaller, more focused helper functions (`drawMainTime`, `drawTopStatusRow`, `drawBottomMessageRow`). This improves readability without changing the templated, stateless `ClockView` architecture that enables host testing.
- **Main loop decomposition**: The monolithic `loop()` function in `main.cpp` was broken down into logical phases: `processBLECommands`, `processButtonInputs`, `updateStateMachine`, `advanceTimekeeping`, `refreshDisplayIfNeeded`, and `managePowerState`. This brings the `loop()` function up to a higher level of abstraction, making it read like a table of contents for the system's heartbeat.
- **Testing resilience**: Relying on CMake/Ninja for host-side unit testing of embedded logic (`firmware/test/`) proved incredibly valuable. Modifying the display rendering and extracting the loop logic could be validated instantly by the host test suite without needing hardware in the loop.

## 37. Python Scripting on Windows: The CP-1252 Trap (verified 2026-09-04)

**The issue:** When automating code refactoring using Python scripts (e.g., using `open(file, 'w')` to rewrite C++ files), non-ASCII characters like em-dashes (`—`), section signs (`§`), and multiplication signs (`×`) became corrupted into character sequences like `â€”` and `Ã—`.

**Why it happened:** Python's built-in `open()` function on Windows defaults to the system's locale encoding (typically `cp1252`), not `utf-8`. If a UTF-8 file containing multibyte characters is read and written without explicitly specifying `encoding='utf-8'`, Python reads the bytes incorrectly and then writes the mangled strings out as new, invalid UTF-8 bytes (a classic double-encoding bug).

**How to prevent it in the future:**
*Always* specify `encoding='utf-8'` when reading and writing source code files in Python, regardless of the platform:
```python
# WRONG (defaults to cp1252 on Windows):
with open('main.cpp', 'w') as f:
    f.write(content)

# CORRECT (forces utf-8):
with open('main.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
```

## 38. Sleep-wake must clear the epoch — a stale time is worse than no time (verified 2026-09-05)

**The bug:** The board has **no RTC**, so `g_epoch` is a software counter that the
firmware advances with `millis()` and updates from BLE writes. After the clock slept
overnight (WFE, up to 6 h), the stored `g_epoch` was still the pre-sleep value. On a
button wake, the state machine re-entered `STATE_RESYNCING`, but `drawClockFace()`
saw `g_epoch > 0` and drew the **running face with the stale time** (e.g. 11:00 pm),
and the sleep check (`isNighttime` on that same stale epoch) immediately put it back
to sleep. The clock showed a time it did not actually know, then vanished.

**The fix:** On every sleep wake, `enterSleepMode()` now clears `g_epoch = 0` (and
`g_last_sync_failed = false`). The clock honestly does not know the current time after
a deep sleep, so it draws a sync screen until Home Assistant writes fresh time. A
failed re-sync after wake then lands on the honest `NO_TIME` state instead of
re-sleeping against a stale time.

**The general principle:** after ANY long/suspending sleep, invalidate cached
time-derived state rather than trusting it. "Show a plausible-sounding but wrong
time" is a worse failure than "I don't know the time yet." The state machine's display
branching `if (g_epoch > 0)` must treat "have a stale value" and "have a valid value"
as different things.

**Note the related `secondsToShutdown` edge case that surfaced the same night:** at
exactly 23:00 the "seconds to next 23:00" formula `(23*3600 - local + 86400) % 86400`
returns 0, making `g_awake_until = millis()` (already expired) → immediate re-sleep.
The fix wraps 0 → 86400. Both were caught with host-harness regression tests written
first (see `ButtonWakeFromSleepDoesNotShowStaleTime` and
`SecondsToShutdownAtExactBedtime`).

## 39. Auto-generate release titles + change summaries from git history (verified 2026-09-05)

A release is more useful when the notes open with a plain-language summary of the
user-facing changes and the title names the headline change, instead of being only a
version number and a static asset list.

We added `.github/workflows/gen_release_notes.py`, invoked by the Release workflow,
which:
- finds the previous `v*` tag (semver-sorted, so v0.10 > v0.9);
- extracts `feat`/`fix` conventional-commit subjects in `prev..tag`;
- writes a body: `# eClock firmware <tag>`, `**Key changes since <prev>:**` with
  bulleted changes, then the standard asset + flash instructions;
- emits a `title=<tag> - <Change Summary>` line to `$GITHUB_OUTPUT`, used by
  `softprops/action-gh-release` as the release `name`.

Design choices worth keeping:
- The workflow needs `fetch-depth: 0` (shallow checkout can't diff against previous
  tags).
- Docs/refactor/chore commits are *excluded* from the headline list so it reads as
  "what changed for the user", not a full log dump. (They're still in the GitHub
  auto-generated changelog link.)
- If there's no previous tag (a first release), fall back to "(initial release)" rather
  than dumping the whole history as "changes".
- Titles are derived, not curated: the phrasing inherits the telegraphic style of the
  commit message. If a release has a headline a human wants to polish, edit it after
  the fact with `gh release edit <tag> --title ...`.


## 40. D9 button cannot wake the board — interrupt dead AND the line reads noisy
            (SPI MISO share makes the pin unusable)

**Symptom** — The shipped eClock has a button that "doesn't react": BUTTON_3 on
D9 (= P1.14), the physical pin that is ALSO the nRF52840 SPI MISO line.
Pressing it did nothing in the running clock.

**Root cause (confirmed by the btn_probe dev tool, v1 -> v4)**
1. `display.init()` calls `SPI.begin()`, which makes the SPI peripheral control
   the MISO pad (D9/P1.14). After that, the **falling-edge GPIOTE interrupt on
   D9 never fires** — its counter stays 0 during the whole test while D1/D2 fire
   many times.
2. While the CPU is awake, D9's **loop poll P climbs continuously even with no
   button press** (the pin is read LOW / floating low), so there is no clean
   poll signal to threshold either. (Both facts were also true immediately after
   trying to reclaim D9 as plain GPIO via `NRF_GPIO->PIN_CNF[46]`.)

**Fix attempted** — `reclaimD9Gpio()`: after `display.init()`, force
`NRF_GPIO->PIN_CNF[46]` (P1.14) back to input + pull-up and re-attach the ISR.
Result: D9 I still stayed 0 => the SPI-peripheral ownership of the pad is not
cleanly unwound with a one-line PIN_CNF write, or the pad is genuinely not
GPIOTE-capable while the panel is powered/SPI-selected.

**Conclusion** — D9: the combined interrupt cannot be restored via the simple
software reclaim this probe tried, and polling is both impossible while the
clock sleeps under WFE and unreliable while awake. **D9 is not a usable wake /
button on this build**. Options for a real solution (only if you need a 3rd
button or a wake button):
- Route a button to a different physical pin that is not on the SPI bus (e.g.
  one of the unused GPIOs on the board / a header pin), and wire that to the
  switch — a hardware change.
- Or accept D1/D2 for wake-only and treat D9 as a hardware defect (reflow).

**Proof tool** — `firmware/proto/btn_probe/` (kept). It boots the panel under
production conditions and shows per-button interrupt("I") + poll("P")
counters on-screen and on serial. Interpretation:
- `I` rises on press => that pin's interrupt works.
- `I=0` but `P` rises cleanly => interrupt dead, pin pollable (potential
  software polling fix — but the clock can't poll in WFE sleep).
- `I=0` AND `P` climbs with no press => pin reads noisy/low => not usable.

**Keep** — btn_probe remains a reusable lessons-learnt instrument for "is THIS
pin driveable / can ITS interrupt fire" questions.

Measured on hardware (v4, idle): D1 I≈6 P≈1, D2 I≈2 P≈1, D9 I=0 P≈0/rising.


## 41. Probe display must mirror the shipped firmware's init exactly — the RST remap is the load-bearing line

**Symptom** — While building the btn_probe dev tool, the e-paper panel would not
come up: it printed `Busy Timeout!` on boot and drew nothing. Iterating the
*render* code (partial windows, manual clears, powerOff calls) never fixed it.

**Root cause** — the probe's `setup()` was missing the mbed-core **RST remap**
that the shipped clock does before `display.init()`:
```cpp
extern PinDescription g_APinDescription[];
g_APinDescription[29].name = P0_15;   // re-bind index 29 to the physical P0.15 RST
display.init(115200, true, 2, false); // init(false) already does a full white refresh
display.setRotation(1);
```
Without that remap, index 29 still points at its default (P0.9), so the panel's
RST line is a no-op, GxEPD2 cannot reset the SSD1680, and every init times out
on BUSY. The render loop was never the problem.

**Unhelpful things I tried before finding it**
- Writing `NRF_GPIO->PIN_CNF[46]` (D9/P1.14) to "reclaim" D9 as GPIO. That
  *tampered with the SPI MISO pin the panel uses* and caused/kept `Busy
  Timeout!`. **Do not touch the display's SPI pins' GPIO config in a probe.**
- Adding `clearScreen()`/`display(false)` before the first draw — a redundant
  extra full refresh that also triggered `Busy Timeout!`.

**The reliable display recipe (matches shipping `main.cpp`)**
1. `#include "pinDefinitions.h"` (provides `PinDescription` + `P0_15`).
2. Remap `g_APinDescription[29].name = P0_15` BEFORE `display.init()`.
3. `display.init(115200, true, 2, false)` then `display.setRotation(1)`.
4. Draw via the paged pattern: `setPartialWindow(0,0,W,H); firstPage();
   do { fillScreen(WHITE); draw(); } while (nextPage());` — no per-draw
   powerOff(), no manual full clears.

A working reference for "thin read-only probe on this hardware" is
`firmware/proto/btn_probe/src/main.cpp` (kept) — it now boots the panel cleanly
under the shipped pattern and reports button Interrupt(I)/Poll(P) counters.


## 44. GxEPD2 leaves the SSD1680 powered after partial updates — explicit `powerOff()` drops idle current

**Context** — In `GxEPD2_BW<...>::nextPage()`, the library automatically calls `epd2.powerOff()`
after a full screen refresh. However, in partial update mode (`setPartialWindow()`), GxEPD2 deliberately
omits `powerOff()` so that subsequent partial draws can execute immediately without re-powering the panel
gate/source voltages or charge pump.

**Finding** — Because the clock only refreshes once per minute during the day, omitting `powerOff()`
means the SSD1680's internal high-voltage booster and charge pumps remain active (`_power_is_on = true`)
for the entire 60-second idle period between updates. This unnecessarily draws tens to hundreds of
microamps continuously from the battery during the entire 18-hour active day.

**Fix** — Calling `display.powerOff();` explicitly immediately after the `do { ... } while (display.nextPage());`
loop in `drawClockFace()` instructs the SSD1680 to power down its internal driving voltages (`0x22, 0x83 -> 0x20`),
allowing the controller to drop to its low quiescent idle state between ticks. GxEPD2's partial refresh
re-powers the necessary rails automatically on the next draw call.


## 45. nRF52840 Internal DC-DC Converter (`NRF_POWER->DCDCEN = 1`)

**Context** — By default, the Arduino/mbed core for the Seeed XIAO nRF52840 leaves the chip's internal
switching DC-DC regulator (REG1) disabled, falling back to the internal linear LDO regulator.

**Finding** — When powered from a 3.3V / 3.7V LiPo rail stepping down to the internal core voltage (~1.3V),
the internal LDO is only ~45–50% efficient, burning the remainder as heat. The hardware includes the
required external LC filter inductors on the XIAO board to support the internal DC-DC converter.

**Fix** — Enabling the DC-DC regulator at the top of `setup()` via:
```cpp
NRF_POWER->DCDCEN = 1;
```
cuts dynamic/active current during CPU operation (framebuffer rendering, SPI transfers, ADC conversions)
and BLE radio activity (advertising, RX/TX) by approximately 30% to 45%.
