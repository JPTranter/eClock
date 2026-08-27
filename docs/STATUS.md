# Project Status

Update this file at the end of each development session. It is the single place to
look to answer "where is this project actually up to?"

Last updated: 2026-08-27

## Current phase

**Phase 6 (Review & Polish) underway.** Power budget analysis written and
documented. Font bumped from 82pt to 88pt (+6pt, 63% → 68% vertical fill).
MAC address now shown on sync screen for HA configuration. "No Time!" text
clipping fixed. Stay-awake behaviour hardened (button press during sleep
keeps clock on until next 11pm, not just 15 min). New board family verified
(identical XIAO nRF52840 Plus + EN05, recognised by Seeed bootloader PID
0x0064). A host test harness compiles the unmodified `main.cpp` on the host
(≥98% line coverage, real PNG renders). **Refactored: extracted the pure logic
into a hardware-free `firmware/src/clock_logic` module (time math, sleep
window, battery classification) with its own focused unit tests, and split
`drawClockFace()` into per-screen renderers; `main.cpp` stays the adapter
layer and all 8 host suites pass (see `docs/research/modularization-proposal.md`).**
**Added a real low-battery warning:
≤20% shows the empty-battery icon with the low %, and ≤5% draws a final
"LOW BATTERY / Charge me now" screen that persists on the panel even after
power loss (a dead clock can't be mistaken for a live-but-stopped one).**
**Fixed a USB bug where a USB-powered clock falsely showed LOW BATTERY on
boot** (`getBatteryPercent()` returns `-1` on USB and `-1 <= 5`; now guarded).
Also added a [User Guide](docs/USER_GUIDE.md), a charging section, and linked
the hardware kit purchase page.**

## Honest state of play

| Area | State |
| --- | --- |
| Project plan | Written and reviewed |
| Hardware selected | Yes, in hand |
| Toolchain | **Verified (mbed-only).** The shipped firmware requires the `mbed` core (`main.cpp` `#error`s without `ECLOCK_CORE_MBED`); the `adafruit` env does not build it. `default_envs = mbed`. |
| Upload path | **Verified.** Serial DFU working on `mbed` core; reset script patched for `0x8045` PID |
|| Firmware | **Phase 6 in progress.** Font bumped 82→88pt (+6pt, 63%→68% vertical fill). MAC address shown on sync screen. "No Time!" text clipping fixed. Button sleep-wake changed to keep clock on until next 11pm. Low-battery warning added (≤20% empty icon, ≤5% final "LOW BATTERY" screen). Sleep shows "Zzz". Builds clean: 353KB flash (44.6%), 73KB RAM (31.5%). |
|| Pinout / board traps | **Verified.** Second board confirmed identical (XIAO nRF52840 Plus + EN05, Seeed bootloader PID 0x0064). |
|| Display driver approach | **Verified.** Font baseline variance now caught by tooling. Colon centering automated. |
|| Power management | **Phase 3 complete — budget now modelled.** Full six-term power model written (`docs/research/power-budget-analysis.md`). Key finding: 3 months not guaranteed at 1-min refresh without measured currents. |
|| BLE time sync (device side) | **Verified.** MAC address now shown on sync screen for HA configuration. |
|| BLE time sync (HA side) | **Verified.** |
|| Font tooling | **Hardened.** `fix` command normalises yOffsets. Sprite auto-sizes cells for any font. |
|| Power budget | **Modelled but unmeasured.** `docs/research/power-budget-analysis.md` — the project's #1 risk now has a reproducible model. Ready for bench measurement. |
|| Enclosure | Notes only, no CAD |
|| Host test harness | **Verified.** `firmware/test/` compiles unmodified `main.cpp` on the host (≥98% line coverage), PNG renders of the clock face. |
|| Low-battery warning | **Verified.** ≤20% empty icon, ≤5% final "LOW BATTERY" screen; USB (VBUS) correctly does not trigger it. |
|| Build/test docs | `README.md` has full CLI build + test + flash steps + doc links + purchase page; `docs/SETUP.md` written and verified |
|| User guide | `docs/USER_GUIDE.md` — screens, icons, charging, troubleshooting (uses real renders) |

## Immediate next steps

1. **Measure power consumption on the bench** — three measurements close the
   37–335 day range: full-board idle current (WFE), partial-refresh peak/avg
   current, and SSD1680 idle current with rail on. Methodology in
   `docs/research/power-budget-analysis.md`.
2. **Decide on daytime full refresh** — the firmware does zero full refreshes
   between 05:00 and 23:00. The ghosting test only validated 53 partials; 1080/day
   is unvalidated. Adding one hourly full refresh costs ~0.01 mAh/day (negligible).
3. **Begin enclosure design** — OnShape, accessible charge port, display
   window, wall-mount and desk-stand options. See `docs/enclosure/DESIGN_NOTES.md`.
4. **Decide refresh interval** — 5-minute refresh comfortably clears 3-month
   target across the whole credible current range (37–335 days → 126+ days).
   Product trade-off: the minute digit only changes every 5 minutes.

## Open questions

- Power consumption is modelled but *unmeasured*. The 37–335 day range is the
  honest answer until bench data replaces it. Three measurements collapse the
  range.
- Does the panel remain legible at the temperatures and light levels where it
  will actually live?
- Should the clock refresh every 1 minute (current) or every 5/10 minutes
  (battery-safe)? The decision is now a product choice, not a feasibility
  question.

## Phase signoff log

| Phase | Signed off | Date | Notes |
| --- | --- | --- | --- |
| 1. Setup and validation | **Yes** | 2026-08-22 | All checks pass with captured evidence |
| 2. Display functionality | **Yes** | 2026-08-22 | Panel alive: full 3374ms, partial 880ms, zero ghosting at 53 partials |
| 3. Power management | **Yes** | 2026-08-23 | Clock face draws with battery monitoring; SAADC pin conflict solved. System ON idle sleep (WFE) running. |
| 4. Bluetooth integration | **Yes** | 2026-08-23 | Working end-to-end with HA. |
| 5. Final refinement | **Yes** | 2026-08-23 | Hardware SPI/GPIO interference and custom font line-wrapping fixed. |
| 6. Tooling polish | **Yes** | 2026-08-24 | Baseline normalised (FontChango82.h). `font_tool.py fix` added. Sprite auto-sizing. Docs updated. |
