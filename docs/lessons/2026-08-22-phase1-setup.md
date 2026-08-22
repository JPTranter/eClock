# Development Session Log — 2026-08-22

## Session Summary

Initialised the git repository, scaffolded the PlatformIO project, and completed and
signed off Phase 1 (environment and board validation). Firmware builds, uploads, and
runs on real hardware with captured serial evidence.

## What Was Learned

### The inherited pin mapping did not compile

The prior notes gave the EN05 pin mapping as `D7`/`D16`/`D11`/`D3`. Two of those macros
do not exist. `variant.h` for `Seeed_XIAO_nRF52840` defines only `D0` through `D10`, so
`D16` and `D11` fail with *"was not declared in this scope"*.

The pin **indices** were correct all along — `g_ADigitalPinMap` in `variant.cpp` maps
index 16 to P0.27 and index 11 to P0.26. The fix is to use bare integers rather than
`Dxx` macros. Lesson: verify pin aliases against the variant source, not against notes.

### `Serial` does not link on the Adafruit core without TinyUSB

A sketch using `Serial` compiles cleanly and then fails at link:

```
undefined reference to `Adafruit_USBD_CDC::begin(unsigned long)'
undefined reference to `Serial'
```

`Serial` is a USB CDC endpoint from `Adafruit_TinyUSB`, and PlatformIO's Library
Dependency Finder does not link the bundled library implicitly. Adding
`#include <Adafruit_TinyUSB.h>` fixes it. A compile-clean/link-fail split like this
points at a missing library, not a missing symbol in your own code.

### UF2 drag-and-drop is not repeatable on this board

The `XIAO-BOOT` mass-storage volume appeared on the first entry into the bootloader and
accepted a hand-converted `.uf2` correctly (verified magics and family ID `0xADA52840`
before copying). After that first flash it never remounted — no drive letter, nothing
in `Get-Volume` — even though the DFU serial port enumerated every time.

Serial DFU via `pio run -t upload` worked on every attempt. Do not build a workflow on
the mass-storage path.

### The board has two USB identities

| State | VID:PID | Port here |
| --- | --- | --- |
| Bootloader | `2886:0064` | COM6 |
| Application | `2886:8044` | COM11 |

Uploads target the bootloader port, serial monitoring the application port. Watching
the PID flip from `0064` to `8044` is a reliable way to confirm an upload actually
activated new firmware, independent of what the uploader claims.

### LEDs here are active HIGH

The variant defines `LED_STATE_ON 1`. I had assumed active low (the common case on
XIAO boards) and would have shipped an inverted heartbeat. Deriving on/off from
`LED_STATE_ON` rather than hardcoding `LOW`/`HIGH` makes the code correct on either
polarity.

### Two pins are shared with things that matter

- P0.26 = ePaper `RST` **and** `LED_RED`
- P0.27 = ePaper `DC` **and** the IMU's I2C SCL

Blinking the red LED in display code would toggle the panel's reset line. The heartbeat
uses `LED_BLUE` (index 12, P0.06) for this reason.

### Phase-locked intervals hide information

The first firmware blinked at 500 ms and reported at 1000 ms, so the report always
sampled the LED at the same point in its cycle and every log line read `led=OFF`. The
LED was fine; the instrumentation was lying. Changed the blink to 400 ms so the sampled
state actually varies. When a diagnostic value never changes, suspect the sampling
before the subject.

### Proprietary files need handling before the first commit, not after

The vendor datasheets went into the initial commit. Untracking them later would have
left the blobs in history permanently. Because the repo had never been pushed, the fix
was to drop the ref, rebuild the commits without the binaries, expire the reflog, and
`git gc --prune=now`, which took `.git` from 12 MB to 133 KB. Verified with
`git log --all --diff-filter=A`, a blob-size sort, and `git fsck`.

Had this been pushed, the only honest options would have been a force-push rewrite
affecting every clone, or accepting the leak. Decide what is committable before the
first commit.

## Challenges Encountered

1. `D16`/`D11` undefined — resolved by reading `variant.h` and `variant.cpp` directly.
2. Link failure on `Serial` — resolved by including `Adafruit_TinyUSB.h`.
3. UF2 volume not remounting — resolved by switching to serial DFU.
4. Boot banner unobtainable: the firmware prints it within ~3 s of reset, before a
   manually started monitor can attach. Resolved with `tools/capture_boot.py`, which
   polls for the application port and opens it the instant it enumerates.
5. Upload failing with the port busy — the capture script was holding it. Sequence
   matters: enter the bootloader first, *then* start the capture, *then* upload.

## Solutions Implemented

- `firmware/platformio.ini` with `adafruit` (default) and `mbed` environments, platform
  pinned to `maxgerhardt/platform-nordicnrf52` (the `_adafruit` boards do not exist in
  the official platform).
- `firmware/include/board_pins.h` — per-core pin map plus the mbed TWIM-disable helper.
- `firmware/src/main.cpp` — heartbeat, banner, tick log, RX echo, D6 gate check.
- `firmware/tools/reset_to_bootloader.py` — idempotent 1200-baud touch into DFU.
- `firmware/tools/capture_boot.py` — wins the enumeration race for the banner.
- `docs/SETUP.md` — verified commands and a pitfalls table.

## Next Steps

Phase 2: add GxEPD2, get pixels on the glass, then characterise partial refresh and
ghosting. The pin mapping and D6 gate are confirmed drivable but the panel itself has
never been driven from this repo.

## Configuration Changes

`.gitignore` now excludes vendor documentation two ways: the datasheets directory
(except its README) and a blanket `*.pdf` / `*_SCH*.zip` / `*_PCB*.zip` rule, so a
stray PDF anywhere in the tree cannot be committed. Tested with dummy files.

## Resources Consulted

- `framework-arduinoadafruitnrf52-seeed/variants/Seeed_XIAO_nRF52840/variant.h` and
  `variant.cpp` — the authoritative pin map
- `platform-nordicnrf52` board JSONs (`xiaoble_adafruit.json`, `xiaoble.json`)
- `INFO_UF2.TXT` on the XIAO-BOOT volume — bootloader and SoftDevice versions
- UF2 file format specification (magic numbers, family IDs)
