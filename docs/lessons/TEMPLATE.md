# Development Session Template

## Date


## Session Summary


## What Was Learned


## Challenges Encountered


## Solutions Implemented


## Next Steps


## Code Changes
```cpp

```

## Configuration Changes


## Resources Consulted


## AI Assistance Summary

## Docs maintenance checklist (do this whenever docs change)

- **Regenerate screenshots.** Any change to a screen renderer, font, icon, version
  text, or layout invalidates `docs/screenshots/*.png`. Regenerate them with the host
  test harness from current firmware:
  ```
  cd firmware/test
  PATH="<mingw64>/bin:$PATH" ctest --test-dir build -R test_display --output-on-failure
  # then copy firmware/test/output/<screen>.png -> docs/screenshots/<screen>.png
  ```
  Verify with `md5sum` that each `docs/screenshots/*.png` matches
  `firmware/test/output/*.png`; commit the refresh in the same commit as the doc change.
- **Keep pin numbers in sync.** `docs/hardware/PINOUT.md` and `docs/lessons/*` both
  document the pin mapping. When `firmware/include/board_pins.h` changes, update every
  table/note that mentions `EPD_CS`/`EPD_DC`/`EPD_RST`/`EPD_BUSY`/`EPD_POWER` in both.
- **Cross-check index vs physical pin.** On the mbed core, `EPD_RST` is a runtime-remapped
  index (29 → P0.15); a `Dxx` alias means something different. Always state which core
  you're describing (mbed raw indices vs Adafruit `Dxx`).
