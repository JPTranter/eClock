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

## Immediate next steps

1. Begin Phase 5 (Final refinement) to clean up code and finalize mechanical integration.
2. Re-visit Phase 3 deep sleep implementation now that BLE is working, to ensure battery life can hit the target.
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
| 5. Final refinement | No | — | |

Phase 3 is now formally complete. We implemented mbed-os System ON idle sleep (WFE) by hooking delay() into the non-blocking main loop, dramatically reducing the CPU duty cycle while maintaining sub-100ms latency for physical buttons and BLE events.
