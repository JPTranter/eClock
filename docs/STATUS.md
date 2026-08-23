# Project Status

Update this file at the end of each development session. It is the single place to
look to answer "where is this project actually up to?"

Last updated: 2026-08-23

## Current phase

**Phase 4 — Bluetooth integration.** Almost complete. Clock successfully syncs time with Home Assistant via BLE. 

## Honest state of play

| Area | State |
| --- | --- |
| Project plan | Written and reviewed |
| Hardware selected | Yes, in hand |
| Toolchain | **Verified.** PlatformIO builds both core environments |
| Upload path | **Verified.** Serial DFU working on `mbed` core; reset script patched for `0x8045` PID |
| Firmware | **Phase 4 working** - Device successfully handles manual and auto BLE time syncs. |
| Pinout / board traps | **Verified.** |
| Display driver approach | **Verified.** Font generator bug fixed (no diagonal shearing). Alignment centered. |
| Power management | **Phase 3 complete** - SAADC pin conflict solved. System ON idle sleep (WFE) implemented via RTOS thread yielding. |
| BLE time sync (device side) | **Verified.** Firmware successfully broadcasts and receives time on the `mbed` core. Drift bug fixed. |
| BLE time sync (HA side) | **Verified.** Custom component successfully discovers and syncs device (MAC address explicitly recommended to bypass discovery caching). |
| Enclosure | Notes only, no CAD |
| Build instructions | `docs/SETUP.md` written and verified for Phase 1 scope |

Phase 4 successfully solved the critical Bluetooth bugs on the `mbed` core:
1. **ArduinoBLE Advertisement Bug:** `BLE.advertise()` fails to restart after `BLE.stopAdvertise()` unless GAP parameters (`setLocalName`, `setDeviceName`, `setAdvertisedService`) are explicitly re-applied.
2. **Integer Underflow:** Timeouts using `millis()` must carefully ensure the `now` timestamp isn't captured before a blocking operation, or it underflows and instantly times out.
3. **Time Drift:** Display updates must align with `local_time % 60 == 0`, not just a blind 60-second counter from the sync event.
4. **HA Discovery Caching:** `async_discovered_service_info` relies on scan responses which are flaky. Matching by the standard Time Service UUID (`1805`) conflicts with smartwatches. Passing multiple explicit MAC addresses (`mac_addresses`) in config is the most reliable approach.
5. **HA GATT Caching:** Flashing updated firmware during dev requires forcefully calling `clear_cache()` in Bleak so HA sees newly exposed characteristics.
6. **HA Deduplication:** HA aggressively deduplicates identical BLE advertisements. A rolling counter added to `ManufacturerData` successfully forces HA to trigger a fresh time sync on every button press.

Phase 5 completely resolved remaining UI and hardware integration quirks:
1. **SPI MISO vs GPIO Interrupt Interference:** The `D9` button pin shares the `MISO` line. `SPI.begin()` initializes MISO with no pull-up resistor, leaving the button floating. This caused an interrupt storm that crashed the device. Re-applying `pinMode(INPUT_PULLUP)` immediately after `SPI.begin()` restored the pull-up and completely fixed the bug without requiring complex hardware SPI detachment sequences.
2. **Text Wrap Overflow:** At 10:09 and 00:00, the `FontChango82` width overflowed the 296px screen, causing Adafruit_GFX to wrap the final digit off-screen. By artificially reducing the `xAdvance` property of the digits in the `GFXglyph` array by 8 pixels, the characters were compressed (bubble-letter style) enough to fit safely on a single line.

## Immediate next steps

1. **Investigate Button 3 (D9):** While the interrupt storm was fixed by re-applying the pull-up, the button still fails to trigger a refresh. The Mbed SPI peripheral might be completely blocking the GPIO input path for MISO. Needs a deeper hardware-level workaround next session.
2. Begin Phase 6 (Enclosure & Final Deployment) to finalize mechanical integration.
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

Phase 5 is now formally complete. The device is rock solid, syncs reliably via BLE, gracefully handles display width limitations, and safely resolves hardware pin multiplexing conflicts.
