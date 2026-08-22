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
| Power management | **Phase 3 baseline running** — SAADC pin conflict solved. Sleep is delay() placeholder. |
| BLE time sync (device side) | **Verified.** Firmware successfully broadcasts and receives time on the `mbed` core. Drift bug fixed. |
| BLE time sync (HA side) | **Verified.** Custom component successfully discovers and syncs device (MAC address explicitly recommended to bypass discovery caching). |
| Enclosure | Notes only, no CAD |
| Build instructions | `docs/SETUP.md` written and verified for Phase 1 scope |

Phase 4 successfully solved the critical Bluetooth bugs on the `mbed` core:
1. **ArduinoBLE Advertisement Bug:** `BLE.advertise()` fails to restart after `BLE.stopAdvertise()` unless GAP parameters (`setLocalName`, `setDeviceName`, `setAdvertisedService`) are explicitly re-applied.
2. **Integer Underflow:** Timeouts using `millis()` must carefully ensure the `now` timestamp isn't captured before a blocking operation, or it underflows and instantly times out.
3. **Time Drift:** Display updates must align with `local_time % 60 == 0`, not just a blind 60-second counter from the sync event.
4. **HA Discovery Caching:** `async_discovered_service_info` is highly susceptible to dropping connectable flags or names from the cache if it misses a scan response. Bypassing it with a direct MAC address is the best workaround.

## Immediate next steps

1. Sign off Phase 4 now that sync works end-to-end.
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
| 3. Power management | Phase 3 baseline | 2026-08-22 | Clock face draws with battery monitoring; SAADC pin conflict solved. Deep sleep deferred. |
| 4. Bluetooth integration | **Yes** | 2026-08-23 | Working end-to-end with HA. |
| 5. Final refinement | No | — | |
