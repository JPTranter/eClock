# Research Tracking

Open questions, their current answers, and what still needs proving. Update the status
field as questions close. "Answered (prior art)" means we have a confident answer from
earlier hands-on work that has not yet been reproduced by code in this repository.

---

## ePaper display library

- **Status:** Answered (prior art) — needs reconfirmation in Phase 1/2
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

- **Status:** Open — decision deferred to Phase 1 revalidation
- **Trade-off:**
  - **Adafruit nRF52 core:** display works flawlessly with `D16`/`D11`. But
    `Bluefruit.begin()` hard-faults, because the XIAO ships with the factory Seeed
    bootloader and `Bluefruit52Lib` requires the Adafruit nRF52 UF2 bootloader.
    Using BLE means reflashing the bootloader (SWD or a UF2 drop).
  - **Seeed mbed core + ArduinoBLE:** no bootloader reflash needed, but the display
    requires remapping DC to `15` / RST to `11` *and* a low-level hack to disable the
    TWIM/TWI peripherals squatting on P0.26/P0.27.
- **Note:** the original suspicion that the hard fault was an `LFRC` vs `LFXO`
  crystal setting was **wrong**. It is a bootloader incompatibility.
- **Decision criteria:** whichever path gets a stable display *and* a stable BLE stack
  with the fewest fragile hacks. Record the decision here once made.

## Power management / deep sleep

- **Status:** Not started — highest project risk
- **Needs:** nRF52840 deep-sleep (System OFF vs System ON idle) characterisation, RTC
  wake sources, measured current in sleep and during a panel refresh, and an energy
  budget showing whether minute-resolution updates fit 500 mAh for three months.
- **Design intent:** partial refresh each minute, periodic full refresh to clear
  ghosting, deep sleep between updates.
- **Watch for:** whether leaving the panel powered (D6 HIGH) between updates costs
  meaningful current, versus the cost of a full re-init each wake.

## Battery monitoring

- **Status:** Not started
- **Needs:** how the XIAO exposes battery voltage (onboard divider, `VBAT` read,
  charge-status pin), ADC reference and calibration, and mapping LiPo voltage to a
  useful remaining-life indication. Sample under load, not mid-refresh.
- **Deliverable:** on-screen battery indicator plus a low-battery warning with enough
  lead time to be actionable.

## BLE time sync — device side

- **Status:** Not started in this repo
- **Design intent:** the clock advertises the Current Time Service
  (`0x1805`) and accepts a write to the Current Time characteristic (`0x2A2B`).
  Payload is 8 bytes: little-endian `int32` Unix epoch followed by little-endian
  `int32` UTC offset in seconds (Python `struct.pack("<ii", epoch, tz_offset)`).
- **Needs:** advertise only in short windows to save power, a manual resync button, a
  retry path for missed syncs, and an on-screen staleness indicator when the last
  successful sync is old.

## BLE time sync — Home Assistant side

- **Status:** Answered (draft committed) — not re-validated
- **Answer:** custom component at
  `integrations/homeassistant/custom_components/epaper_clock/`. It registers a
  Bluetooth callback matched on service UUID `0x1805`, connects on a fresh
  advertisement using `bleak_retry_connector.establish_connection`, and writes the
  packed epoch and UTC offset. It filters advertisements older than 5 s, applies a
  15 s cooldown against redundant syncs, and exposes an `epaper_clock.sync_time`
  service for manual triggering (which bypasses the cooldown).
- **Still to prove:** end-to-end against real firmware, including behaviour when the
  clock is asleep and not advertising.

## Daylight saving time

- **Status:** Not started
- **Approach:** the device stores epoch plus a UTC offset supplied by Home Assistant,
  so DST correctness depends on resyncing after a transition rather than on any
  onboard timezone database. Needs a test plan covering a DST change while offline.

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
