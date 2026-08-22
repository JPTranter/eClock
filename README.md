# eClock — a low-power ePaper clock

An inexpensive, reliable ePaper clock built to solve one specific problem: helping
people leave the house on time for church. It shows accurate time on an always-on
ePaper panel, runs for months on a single small LiPo, and optionally syncs its clock
from Home Assistant over Bluetooth Low Energy.

Status: **Phase 1 — setup and environment validation.** No firmware is committed yet.
See `docs/PROJECT_PLAN.md` for the phase plan and `docs/STATUS.md` for where things
actually stand today.

## Design goals

- Readable at a glance from across a room.
- Battery life measured in months, not days, with a usable low-battery warning.
- Correct time without user intervention, including across daylight saving changes.
- Reproducible by someone else from the documentation in this repo.
- Fully open source: firmware, enclosure, and build instructions.

## Hardware

| Part | Detail |
| --- | --- |
| Microcontroller | Seeed Studio XIAO nRF52840 |
| Display shield | Seeed Studio XIAO ePaper Display Board (EN05) |
| Display panel | 2.9" monochrome ePaper, 296 x 128, GDEY029T94 / FPC-A005 |
| Panel controller | SSD1680 |
| Battery | Rechargeable LiPo, 500 mAh |
| Enclosure | 3D printed, designed in OnShape |

Pinout, power-rail gotchas, and board-specific traps are documented in
`docs/hardware/PINOUT.md`. Read that before wiring or writing display code — the
EN05 does not talk to the panel over the visible header pins.

## Repository layout

```
docs/
  PROJECT_PLAN.md            Phased development plan and success criteria
  STATUS.md                  Current phase, what is done, what is next
  SETUP.md                   Verified build/flash/monitor instructions
  hardware/PINOUT.md         Pin mappings, power control, board traps
  hardware/datasheets/       Vendor docs — NOT committed, see README there
  lessons/LESSONS_LEARNT.md  Accumulated hard-won findings
  lessons/TEMPLATE.md        Per-session log template
  research/                  Open research questions and their answers
  testing/                   Test checklists
  enclosure/                 Enclosure design notes
firmware/
  platformio.ini             Two envs: adafruit (default) and mbed
  src/main.cpp               Phase 1 blink + serial validation firmware
  include/board_pins.h       EN05 pin map, per-core
  tools/                     Flash and serial-capture helpers
hardware/enclosure/          (Phase 5) OnShape exports — not yet created
integrations/homeassistant/  Home Assistant custom component for BLE time sync
```

Note on vendor documentation: the Seeed and Good Display datasheets, schematic and PCB
archives are proprietary and are **not** included in this repository. See
`docs/hardware/datasheets/README.md` for what to download and where. Everything this
project learned from them is written up in plain terms in `docs/hardware/PINOUT.md`.

## Home Assistant time sync

`integrations/homeassistant/custom_components/epaper_clock/` contains a custom
component that discovers the clock by BLE advertisement and writes the current epoch
plus UTC offset to the standard Current Time characteristic (`0x2A2B`). It exposes an
`epaper_clock.sync_time` service for manual resyncs. This host-side code is drafted
but has not been re-validated against committed firmware — see `docs/STATUS.md`.

## Building this yourself

Build instructions are a Phase 5 deliverable and do not exist yet. The plan, the
hardware notes, and the lessons file are accurate and useful today; treat everything
else as work in progress.

## License

MIT — see `LICENSE`.
