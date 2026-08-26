# Third-Party Notices

The host test fixture (`firmware/test/third_party/`) vendors a small number of
third-party components so that `firmware/test/` builds offline and reproducibly.
This file records what they are, where they came from, and their licenses.

All vendored copies retain their original license text (`license.txt` where the
upstream ships one, or the header comment otherwise). Nothing here was modified
from upstream except to include only the files the test build needs.

## Adafruit GFX Library

- **What:** Core graphics primitives + `GFXcanvas1` (the 1-bit framebuffer the
  fake display uses) + the two FreeSans bitmap fonts the firmware renders with.
- **Files:** `Adafruit_GFX.h`, `Adafruit_GFX.cpp`, `glcdfont.c`, `gfxfont.h`,
  `Fonts/FreeSans9pt7b.h`, `Fonts/FreeSansBold24pt7b.h`, `license.txt`
- **Upstream:** https://github.com/adafruit/Adafruit-GFX-Library
- **Version:** 1.12.6 (tag `1.12.6`), matching the version PlatformIO resolves
  for the firmware builds.
- **License:** BSD 3-clause — © 2012 Adafruit Industries. See `license.txt`.

### Font bitmap provenance (FreeSans)

`Fonts/FreeSans9pt7b.h` and `Fonts/FreeSansBold24pt7b.h` are bitmap
rasterisations derived from GNU FreeFont. GNU FreeFont is licensed under the
GPLv3 with a font-embedding exception. Adafruit distributes these headers
under its own BSD header, but the underlying font outlines carry GPL
provenance.

- **Practical impact:** none for this project's use — the firmware already
  links these same headers on-device via the PlatformIO `Adafruit GFX Library`
  dependency, so vendoring a host-test copy changes nothing about the
  project's license posture. The GPL only constrains *distribution* under an
  incompatible license.
- **If the project is ever distributed under a non-GPL license:** replace these
  two headers with an OFL-licensed font (e.g. Chango, already used for the
  large time digits) and regenerate via `firmware/tools/font_tool.py`.

## stb_image_write.h

- **What:** Single-header PNG encoder used by `harness/png_dump.cpp` to write
  the display-render PNGs.
- **Files:** `stb_image_write.h`
- **Upstream:** https://github.com/nothings/stb
- **Version:** v1.16
- **License:** Public domain (also available under MIT). See the header comment.

## Not vendored (resolved at build time)

- **GoogleTest** — fetched by CMake via `FetchContent` at configure time
  (`v1.14.0`, BSD 3-clause). Not committed; network required on first
  configure only.
- **MinGW-w64 GCC toolchain** — a local install prerequisite (see
  `docs/lessons/LESSONS_LEARNT.md` §22), not part of this repository.
