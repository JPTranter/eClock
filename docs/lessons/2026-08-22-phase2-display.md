# Development Session Log — 2026-08-22 (Phase 2)

## Session Summary

Completed Phase 2: GxEPD2 integration, panel bring-up, and refresh characterisation.
The panel is alive and producing well-timed, ghost-free partial updates. Phase 1 was
also signed off.

## What Was Learned

### The XIAO nRF52840 Plus is a different board from the standard XIAO

This is the lesson that ate most of the session. The standard XIAO nRF52840 variant
declares 33 pins; the Plus variant declares 39. Crucially, `D11` and `D16` resolve to
entirely different physical ports:

| Symbol | Stock variant | Plus variant (this board) |
| --- | --- | --- |
| D11 | index 11 → **P0.26** | index 30 → **P0.15** |
| D16 | not defined | index 35 → **P0.31** |

Without `board_build.variant = Seeed_XIAO_nRF52840_Plus`, the D11/D16 symbols either
fail to compile or, if bare integers are used, resolve to the wrong physical pins.
GxEPD2 sees no responses from the panel and reports "Busy Timeout!" every 10 seconds
indefinitely. This is indistinguishable from a defunct panel and sent the work down a
false trail for over an hour.

### A pin diagnostic is only as good as the variant it runs against

A pull-up/pull-down scan correctly identified which physical pins were driven by an
attached device, but I was interpreting bare indices 11 and 16 against the stock
variant, so I measured the wrong physical pins entirely and falsely concluded the
panel must be wired to D0..D3. The diagnostic tool itself was sound — the mistake was
not knowing which variant was compiled in. Lesson: never use bare indices without
confirming the compiled-in variant first, or better, always use the `Dxx` symbols
that the variant defines.

### The board has no 32 kHz crystal

The Plus lacks the external 32 kHz crystal present on some nRF52840 modules. Without
`USE_LFRC`, the BLE stack's low-frequency clock source never starts and
`Bluefruit.begin()` hangs forever. The old project's platformio.ini explicitly
unflagged `USE_LFXO` and set `USE_LFRC` — this was not documented in the inherited
notes but was essential for correct operation.

### Partial refresh is faster and cleaner than expected

On real hardware:
- Full refresh: 3,374 ms (visible flash)
- Partial refresh: 880 ms average, 879–881 ms range over 53 samples — zero jitter
- Zero visible ghosting after 53 consecutive partials

This is significantly better than the plan assumed. A conservative baseline of one
full refresh per hour gives 24 × 3.4s + 1416 × 0.88s ≈ 22 minutes of active time
per day. The 3-month battery target with a 500 mAh LiPo is achievable with moderate
sleep currents.

### The old project had working code the whole time

The `eClock-old` sibling directory contained a working PlatformIO project with the
correct Plus variant, pin definitions, and `USE_LFRC` flag. Crucially, its
`src/main.cpp` used `D7`/`D16`/`D11`/`D3`/`D6` — the same symbols as this project —
but its `platformio.ini` included the variant line that makes them resolve to the
right physical pins. The lessons file from that project described the pogo-pin trap
and the `Bluefruit.begin()` hard fault, but did not document the Plus variant
requirement because it was captured in `platformio.ini` rather than in prose.

Lesson for future Phase 1 work: when inheriting a project, diff the platformio.ini
first. A missing `board_build.variant` line can waste hours.

## Challenges Encountered

1. GxEPD2 reported "Busy Timeout!" consistently — traced to the stock XIAO variant
   resolving D11/D16 to the wrong physical pins.
2. Pin diagnostic produced misleading results because it ran against the wrong variant.
3. The inherited lessons file described a pogo-pin board generation that does not
   apply to the Plus, creating false leads.

## Solutions Implemented

- Plus variant vendored at `firmware/variants/Seeed_XIAO_nRF52840_Plus`
- `platformio.ini`: `board_build.variant` line added, `USE_LFRC` set
- `board_pins.h`: rewritten with compile-time guard (fails on wrong PINS_COUNT),
  Dxx-only pin definitions, and an explanation of the variant trap
- Phase 2 interactive display harness running on the board

## Next Steps

Phase 3: power management. Measure sleep current, implement deep sleep between
updates, add battery monitoring and low-battery warning, validate the energy budget
against the 3-month target.
