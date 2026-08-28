# Phase 1 — Setup and Environment Validation Checklist

Goal: prove the toolchain and the board work end to end before any clock logic exists.

**Status: PASSED and SIGNED OFF 2026-08-22.** Evidence is the captured serial output
below.

## Toolchain

- [x] PlatformIO CLI (`pio`) available on PATH — 6.1.19
- [x] XIAO nRF52840 platform resolves (maxgerhardt fork for `_adafruit` boards)
- [x] Both Arduino cores build: `adafruit` and `mbed` environments defined
- [x] `firmware/platformio.ini` committed and `pio run -e adafruit` completes cleanly
- [x] Footprint recorded: RAM 3.0% (7,148 B), Flash 5.5% (44,668 B)
- [ ] VS Code + PlatformIO IDE extension (optional, not required — CLI verified)

## Board bring-up

- [x] Board enumerates over USB
- [x] Both USB identities identified: bootloader `2886:0064`, application `2886:8044`
- [x] Reset into bootloader works (1200-baud touch, `tools/reset_to_bootloader.py`)
- [x] Firmware uploads successfully (serial DFU → "Device programmed.")
- [x] Application runs after upload (PID flips to `8044`)
- [x] LED blinks at the expected rate (400 ms toggle, alternating in tick log)
- [x] Serial monitor prints at 115200 baud
- [x] Host → device serial path works (byte echo confirmed: `0x51 ('Q')`)

## Reproducibility

- [x] Every command captured in `docs/SETUP.md`
- [x] Pitfalls table written from real failures hit during setup
- [x] Port numbers flagged as machine-specific, with a discovery snippet provided
- [x] Helper scripts committed: `tools/reset_to_bootloader.py`, `tools/capture_boot.py`

## Sanity checks before Phase 2

- [x] `D6` power gate can be driven (asserted then released, no fault)
- [x] Pin indices confirmed against the variant's `g_ADigitalPinMap`
- [x] Pin-sharing hazards identified: P0.26 = RST *and* `LED_RED`; P0.27 = DC *and* IMU SCL
- [ ] Minimal GxEPD2 output visible on the panel — **deferred to Phase 2**
- [ ] Battery connector and charge circuit confirmed — **deferred**, needs the LiPo

## Evidence

Captured from a cold boot on 2026-08-22 (`tools/capture_boot.py`):

```
===============================================
  eClock - Phase 1 environment validation
===============================================
  Core       : Adafruit nRF52
  Built      : Aug 22 2026 16:04:34
  Heartbeat  : LED_BLUE @ 1.25 Hz (on=HIGH)
  Panel pins : CS=7 DC=16 RST=11 BUSY=3 PWR=6
-----------------------------------------------
  Expect one tick line per second.
  Type any character to test the RX path.
===============================================

[init] Asserting panel power gate (D6) ... ok, released (panel stays off in Phase 1)

[tick 1] uptime 1s  led=OFF
[tick 2] uptime 2s  led=ON
[echo] received byte 0x51 ('Q')
[tick 3] uptime 3s  led=ON
[tick 4] uptime 4s  led=OFF
[tick 5] uptime 5s  led=OFF
[tick 6] uptime 6s  led=ON
```

## Signoff

| | |
| --- | --- |
| Date | 2026-08-22 |
| Core chosen | **Deferred.** `adafruit` is the default and is what was flashed and verified. The BLE-vs-display trade-off cannot be settled until Phase 2 (display) and Phase 4 (BLE) are attempted; the `mbed` environment is kept buildable so the choice stays open. |
| Toolchain | PlatformIO 6.1.19, platform `maxgerhardt/platform-nordicnrf52`, framework `framework-arduinoadafruitnrf52-seeed 1.10101.0`, gcc-arm-none-eabi 7.2.1 |
| Upload method | Serial DFU via nrfutil (primary). UF2 drag-and-drop also works and is now verified (2026-08-28) with a canonical-format UF2 — see `docs/SETUP.md`. |
| Notes | Three real bugs found and fixed: nonexistent `D11`/`D16` macros, missing `Adafruit_TinyUSB.h` include causing link failure, and a blink/report phase-lock that made the LED state column useless. All are documented in `docs/SETUP.md`. |
| Signed off | **Yes — 2026-08-22** |
