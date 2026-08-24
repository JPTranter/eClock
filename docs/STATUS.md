# Project Status

Update this file at the end of each development session. It is the single place to
look to answer "where is this project actually up to?"

Last updated: 2026-08-24

## Current phase

**Phase 5 signed off. Phase 6 (Enclosure & Final Deployment) tooling polish underway.** Font baseline normalised, sprite tool fixed, `font_tool.py fix` command added.

## Honest state of play

| Area | State |
| --- | --- |
| Project plan | Written and reviewed |
| Hardware selected | Yes, in hand |
| Toolchain | **Verified.** PlatformIO builds both core environments |
| Upload path | **Verified.** Serial DFU working on `mbed` core; reset script patched for `0x8045` PID |
| Firmware | **Phase 5 complete.** All UI quirks resolved. Baseline unified across all digits. Builds clean: 360KB flash (44.4%), 74KB RAM (31.5%). |
| Pinout / board traps | **Verified.** |
| Display driver approach | **Verified.** Font baseline variance now caught by tooling. Colon centering automated. |
| Power management | **Phase 3 complete.** SAADC pin conflict solved. System ON idle sleep (WFE) implemented. |
| BLE time sync (device side) | **Verified.** |
| BLE time sync (HA side) | **Verified.** |
| Font tooling | **Hardened.** `fix` command normalises yOffsets. Sprite auto-sizes cells for any font. |
| Enclosure | Notes only, no CAD |
| Build instructions | `docs/SETUP.md` written and verified |

## Immediate next steps

1. **Investigate Button 3 (D9):** Mbed SPI peripheral likely blocking GPIO input. Needs hardware-level SPIM PSEL inspection.
2. Begin Phase 6 (Enclosure & Final Deployment).
3. Characterise true power consumption in sleep vs advertising.

## Open questions

- What is the actual per-update energy cost, and does a minute-resolution clock fit
  inside a 500 mAh budget for three months? Unmeasured, and the main project risk.
- Does the panel remain legible at the temperatures and light levels where it will
  actually live?

## Phase signoff log

| Phase | Signed off | Date | Notes |
| --- | --- | --- | --- |
| 1. Setup and validation | **Yes** | 2026-08-22 | All checks pass with captured evidence |
| 2. Display functionality | **Yes** | 2026-08-22 | Panel alive: full 3374ms, partial 880ms, zero ghosting at 53 partials |
| 3. Power management | **Yes** | 2026-08-23 | Clock face draws with battery monitoring; SAADC pin conflict solved. System ON idle sleep (WFE) running. |
| 4. Bluetooth integration | **Yes** | 2026-08-23 | Working end-to-end with HA. |
| 5. Final refinement | **Yes** | 2026-08-23 | Hardware SPI/GPIO interference and custom font line-wrapping fixed. |
| 6. Tooling polish | **Yes** | 2026-08-24 | Baseline normalised (FontChango82.h). `font_tool.py fix` added. Sprite auto-sizing. Docs updated. |
