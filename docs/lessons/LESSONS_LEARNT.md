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

### Getting the Display Working on mbed
Because the mbed core doesn't officially have a variant definition for the XIAO nRF52840 **Plus** (only the base/Sense model), the extra pins (`D11..D19`) are missing from its Arduino pin mapping array (`g_APinDescription`). 
To get `GxEPD2` to toggle the reset pin (`P0.15`), we had to manually patch `~/.platformio/packages/framework-arduino-mbed-seeed/variants/SEEED_XIAO_NRF52840_SENSE/variant.cpp` to append `{ P0_15, NULL, NULL, NULL }` to the end of the array (becoming index 33). We then passed index `32` for `P0.31` (DC) and index `33` for `P0.15` (RST) in `board_pins.h`.

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

## 17. Baseline variance across font glyphs (verified 2026-08-24)

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
