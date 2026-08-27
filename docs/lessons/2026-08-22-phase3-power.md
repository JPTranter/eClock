# Development Session Log — 2026-08-22 (Phase 3)

## Session Summary

Started Phase 3, hit a P0.31 hardware sharing problem, and learned a deep lesson about
the nRF52840's SAADC peripheral pin claims.

## What Was Learned

### P0.31 is shared between EPD_DC and VBAT — a hardware conflict the Arduino API cannot resolve

The Seeed XIAO nRF52840 **Plus** variant maps both `D16` (EPD_DC on the EN05 shield)
and `PIN_VBAT` to the same physical pin: P0.31. Both are declared as integer index 35
in the variant. When the board was designed, the assumption was that these would not be
used concurrently — the ePaper display uses SPI, and the battery voltage is an analog
read. But a clock that needs to do both has a problem.

The symptom: every `analogRead(PIN_VBAT)` permanently breaks the ePaper display.
GxEPD2 reports `_Update_Full : 0` (zero microseconds, no update). The panel shows
whatever was on it before — unchanged — with no error, warning, or indication anything
went wrong.

Why: `analogRead()` activates the nRF52840's SAADC (Successive Approximation ADC)
peripheral with a PSEL (Pin SELect) claim on P0.31. On the nRF52840, a peripheral
pin claim overrides GPIO operations at the hardware level. The Arduino framework's
`pinMode(OUTPUT)` sets the GPIO direction bit but does **not** clear the SAADC's
PSEL register. Even after `pinMode(OUTPUT)`, the SAADC still owns the pin, and
GxEPD2's attempts to toggle the DC line are silently swallowed.

### The fix requires direct nRF52 register manipulation

Five register operations are needed; `pinMode()` alone is not enough:

1. **Stop the SAADC task** and wait for acknowledgement
2. **Disable the SAADC peripheral** entirely
3. **Clear all 8 channel PSEL registers** (set to `0xFFFFFFFF` = disconnected)
4. **Write PIN_CNF directly** to standard GPIO output configuration
5. **Call pinMode() as a belt-and-suspenders** so the Arduino framework's internal pin tracking stays consistent

### Partial refresh only works within a single power-on session

The SSD1680 controller has **volatile** internal frame buffer RAM. Cutting the panel's
power rail (D6 MOSFET) erases it. When power is restored, GxEPD2 must:
- Full init with hardware clear (3.4s)
- Full draw to establish a baseline
- THEN partials (880ms) can work, because GxEPD2 diffs the new frame against the
  old buffer that it knows the controller still holds

So the design question for Phase 3 is **not** "can we save power by cutting the rail?"
— we know that costs 3.4s per wake. The question is: **does the SSD1680's idle current
with the rail on exceed what we'd save by sleeping?**

### Battery reading, power cycling, and the real Phase 3 question

What we actually need from Phase 3:
- Measure idle current with panel rail ON (SSD1680 powered, idle)
- Measure idle current with panel rail OFF
- Measure current during a partial refresh (880ms window)
- Decide Option A (rail always on, partials forever) vs. hybrid (rail on for 60
  partials/hour, then off between sessions)

The current firmware implements Option A as the baseline. It reads the battery and
draws the clock every 5 seconds (delay placeholder) — **this is intentionally** for
ease of observation during development, not for production usage.

### The diagnostic method matters: proving vs. assuming

The blind alley with P0.31 was resolved by a structured isolation test:
1. Draw without battery read — proved the hardware was fine
2. Draw after battery read — proved the read was the trigger
3. Battery read BEFORE display init (no overlap) — proved timing didn't matter;
   the SAADC claim persisted regardless
4. SAADC peripheral release — proved the PSEL register was the root cause

When a subsystem breaks after adding a feature, isolate the feature rather than
guessing at the mechanism.

## Challenges Encountered

1. Panel silently failed all draws after any `analogRead(PIN_VBAT)` — traced to SAADC
   PSEL claim on shared P0.31
2. `pinMode(OUTPUT)` insufficient — needed direct NRF_SAADC and NRF_GPIO register writes
3. Initial Phase 3 firmware power-cycled the panel between ticks, which broke partial
   refresh (SSD1680 controller lost its frame buffer memory)

## Solutions Implemented

- `readBatteryVoltage()` with full SAADC peripheral release: stops the task, disables
  the peripheral, clears all PSEL channels, resets PIN_CNF, then calls pinMode(OUTPUT)
  for framework consistency
- Panel power rail kept on between ticks (Option A baseline)
- Clock face with battery icon, voltage, and refresh counters

## Next Steps

1. Measure idle current and refresh energy with a multimeter or USB power meter
2. Decide the final power strategy based on real measurements
3. Implement proper RTC-timed sleep (replacing delay())
4. Add low-battery warning
5. Measure actual partial count between full refreshes that the panel tolerates
   on this clock face design (not just the burst-number test)
