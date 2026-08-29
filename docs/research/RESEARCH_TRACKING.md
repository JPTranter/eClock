# Research Tracking

Open questions, their current answers, and what still needs proving. Update the status
field as questions close. "Answered (prior art)" means we have a confident answer from
earlier hands-on work that has not yet been reproduced by code in this repository.

---

## ePaper display library

- **Status:** Answered and verified (Phase 2 signoff). GxEPD2 with driver class
  `GxEPD2_290_T94_V2` is in production use in `firmware/src/main.cpp`, and the host
  test harness renders real PNG output through it.
- **Answer:** Use **GxEPD2** with driver class `GxEPD2_290_T94_V2`, which maps to the
  SSD1680 controller on this panel.

  ```cpp
  GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT>
      display(GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
  ```

  It supports proper hardware clears, differential partial updates, and
  double-buffering. Use Adafruit GFX fonts (e.g. `Fonts/FreeSans9pt7b.h`) rather than
  the default GLCD font — they scale far more cleanly.
- **Rejected:** `Seeed_GFX` (the manufacturer library). It cannot do true partial
  updates because it never pushes the `OLD_COLORS` buffer, so new text stacks on old
  and ghosts badly. It also force-sleeps the panel after every update and its
  `wake()` is broken on this panel revision, and it has landscape-rotation viewport
  bugs. Details in `docs/lessons/LESSONS_LEARNT.md`.
- **Still to prove:** partial-refresh count before ghosting requires a full refresh.

## Arduino core selection

- **Status:** Answered — **mbed core, decided.** The shipped `main.cpp` hard-requires it
  (`#error "Must compile with mbed core for ArduinoBLE!"` unless `ECLOCK_CORE_MBED` is
  defined), so the `adafruit` env does not build the current firmware.
  `platformio.ini` sets `default_envs = mbed`.
- **Trade-off (as it stood):**
  - **Adafruit nRF52 core:** display works flawlessly with `D16`/`D11`. But
    `Bluefruit.begin()` hard-faults, because the XIAO ships with the factory Seeed
    bootloader and `Bluefruit52Lib` requires the Adafruit nRF52 UF2 bootloader.
    Using BLE means reflashing the bootloader (SWD or a UF2 drop).
  - **Seeed mbed core + ArduinoBLE:** no bootloader reflash needed, but the display
      requires a small runtime remap (see `docs/lessons/LESSONS_LEARNT.md` §5): the mbed
      variant's pin table lacks `P0_15` (RST), so `setup()` rebinds unused index 29 to
      `P0_15`; DC/P0.31 is already present at index 32. Care around the P0.31 DC/VBAT
      overlap (§2).
- **Note:** the original suspicion that the hard fault was an `LFRC` vs `LFXO`
  crystal setting was **wrong**. It is a bootloader incompatibility.
- **Decision:** mbed + ArduinoBLE works without reflashing the bootloader, so it was
  chosen. Recorded here; the `adafruit` env is kept only for reference.

## Power management / deep sleep

- **Status:** **Modelled (2026-08-25).** `docs/research/power-budget-analysis.md`
  provides a six-term, reproducible energy model. Every timing value is measured
  (partial 880ms, full 3374ms); every current value is flagged as assumed.
- **Key finding:** 3-month goal is **not guaranteed** at 1-minute refresh.
  The plausible current range (4–40 mA refresh, 3–50 µA idle) gives 37–335
  days of battery life. Slowing to 5-minute refresh clears 3 months across
  the whole range.
- **Still to prove:** actual measurements of full-board idle current, peak
  partial-refresh current, and SSD1680 standby current. Three measurements
  collapse the 37–335 day range.
- **Also noted:** the firmware does zero daytime full refreshes, but the
  ghosting test only validated 53 consecutive partials. 1080/day is
  unvalidated. Decide on a periodic full refresh (negligible power cost).

## Battery monitoring

- **Status:** Answered and implemented. The clock reads battery voltage through the
  onboard divider and shows a percentage on the running face. It has a two-tier
  low-battery warning: **≤20%** swaps the battery icon for an empty battery, and
  **≤5%** draws a final "LOW BATTERY / Charge me now" screen that persists on the
  ePaper even after power loss.
- **Needs:** bench measurement to validate the 3.3V→4.2V percentage mapping and
  confirm the warning fires with useful lead time on real hardware.
- **Deliverable:** on-screen battery indicator plus a low-battery warning with enough
  lead time to be actionable. **Done** (see `firmware/src/main.cpp`).

## BLE time sync — device side

- **Status:** Answered and implemented (Phase 4 signoff: working end-to-end).
- **Design (as implemented):** the clock advertises the Current Time Service
  (`0x1805`) and accepts a write to the Current Time characteristic (`0x2A2B`).
  Payload is 8 bytes: little-endian `int32` Unix epoch followed by little-endian
  `int32` UTC offset in seconds (Python `struct.pack("<ii", epoch, tz_offset)`).
- **Implemented:** short-window advertising, a manual resync button, a retry path for
  missed syncs, and an on-screen staleness indicator (the sync-status icon + last
  sync time).

## BLE time sync — Home Assistant side

- **Status:** Answered and validated end-to-end (Phase 4 signoff). Custom component at
  `integrations/homeassistant/custom_components/epaper_clock/`. It filters
  advertisements by the clock's MAC address (via a `mac_addresses` config array —
  it does NOT match on service UUID alone, which caused conflicts with other devices
  broadcasting the same service), connects on a fresh advertisement using
  `bleak_retry_connector.establish_connection`, and writes the packed epoch and UTC
  offset. It filters advertisements older than 5 s, applies a 15 s cooldown against
  redundant syncs, and exposes an `epaper_clock.sync_time` service for manual
  triggering (which bypasses the cooldown).
- **Future:** surface the clock's battery level and last-sync time as HA entities
  (not yet implemented).

## Daylight saving time

- **Status:** Partially answered. The device stores epoch plus a UTC offset supplied by
  Home Assistant, so DST correctness depends on resyncing after a transition rather
  than on any onboard timezone database. The host test harness exercises the
  DST-immune sleep (a fixed 6-hour window, so a mid-sleep DST shift cannot move the
  wake time). A full test plan covering a DST change while offline is still to be
  written.

## Enclosure

- **Status:** Not started
- **Needs:** OnShape model, accessible charge port, display window, wall-mount and
  desk-stand options, battery-safe wall thickness. See
  `docs/enclosure/DESIGN_NOTES.md`.

---

## Reference resources

- Seeed EN05 wiki: https://wiki.seeedstudio.com/epaper_en05/
- 2.9" panel product page:
  https://www.seeedstudio.com/2-9-Monochrome-ePaper-Display-with-296x128-Pixels-p-5782.html
- GxEPD2: https://github.com/ZinggJM/GxEPD2
- Vendor datasheets: not committed (proprietary) — see
  `docs/hardware/datasheets/README.md` for download links
