# Project Status

Update this file at the end of each development session. It is the single place to
look to answer "where is this project actually up to?"

Last updated: 2026-08-22

## Current phase

**Phase 2 — Display functionality.** In progress. Phase 1 signed off 2026-08-22.

## Honest state of play

| Area | State |
| --- | --- |
| Project plan | Written and reviewed |
| Hardware selected | Yes, in hand |
| Toolchain | **Verified.** PlatformIO 6.1.19 builds both core environments |
| Upload path | **Verified.** Serial DFU; UF2 drag-and-drop rejected as unreliable |
| Firmware | **Phase 1 blink/serial test committed, flashed, and running** |
| Pinout / board traps | Partly verified — indices confirmed against the variant pin map; panel not yet driven |
| Display driver approach | GxEPD2 chosen from prior art, **not yet exercised** |
| Power management | Not started — this is the genuinely unknown work |
| BLE time sync (device side) | Not started |
| BLE time sync (HA side) | Draft component committed, not re-validated |
| Enclosure | Notes only, no CAD |
| Build instructions | `docs/SETUP.md` written and verified for Phase 1 scope |

Phase 1 corrected three things the prior notes had wrong or omitted:

1. `D11` and `D16` **do not exist as macros** in the Adafruit variant — only `D0`–`D10`
   are defined. The pin *indices* 11 and 16 are correct, so bare integers must be used.
   The old notes would not have compiled.
2. `Serial` on the Adafruit core is TinyUSB CDC and needs an explicit
   `#include <Adafruit_TinyUSB.h>`, or the link fails with `undefined reference to
   'Serial'`. Not mentioned anywhere in the prior notes.
3. P0.26 is **both** ePaper RST and `LED_RED`, and P0.27 is **both** DC and the IMU's
   I2C SCL. The pin-sharing was known for the mbed trap but not flagged as a hazard on
   the Adafruit core.

The remaining findings in `docs/lessons/LESSONS_LEARNT.md` — GxEPD2 behaviour, partial
refresh, the Bluefruit bootloader hard fault — are still unverified prior art.

## Immediate next steps

1. Sign off Phase 1 (or send it back with findings).
2. Begin Phase 2: add GxEPD2, drive the panel, and confirm the D6 + pin mapping story
   end to end with something visible on the glass.
3. Characterise partial refresh: how many before ghosting forces a full refresh.
4. Design the clock face layout for 296x128.

## Open questions

- Which core do we commit to? Still open by design. `adafruit` is flashed and working
  for display-oriented work; the BLE cost (bootloader reflash) is not yet paid.
- How many partial refreshes before ghosting demands a full refresh? Unmeasured.
- What is the actual per-update energy cost, and does a minute-resolution clock fit
  inside a 500 mAh budget for three months? Unmeasured, and the main project risk.
- Does the panel remain legible at the temperatures and light levels where it will
  actually live?

## Phase signoff log

| Phase | Signed off | Date | Notes |
| --- | --- | --- | --- |
| 1. Setup and validation | **Yes** | 2026-08-22 | All checks pass with captured evidence |
| 2. Display functionality | In progress | — | GxEPD2 on the real panel |
| 3. Power management | No | — | Highest risk phase |
| 4. Bluetooth integration | No | — | HA side drafted |
| 5. Final refinement | No | — | |
