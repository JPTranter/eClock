# 2026-08-25: Phase 6 Power Analysis, Font Size Bump & UX Polish

## Summary

Three things happened in this session: (1) a full power budget analysis was
written, closing the project's highest-risk open question, (2) the time font
was bumped from 82pt to 88pt after discovering the xAdvance squish had freed
unused horizontal headroom, and (3) two UX polish items were addressed — the
MAC address is now visible on the sync screen and the "Press any button to
sync" text is no longer clipped.

## Changes

### 1. Power Budget Analysis (docs/research/power-budget-analysis.md)

The project has had an open question since Phase 3: "does a minute-resolution
clock fit inside a 500 mAh budget for three months?" Every prior estimate in
the docs was based on an optimistic "8 mA refresh / 3 µA idle" assumption that
was never measured. The analysis builds a reproducible six-term model with
every timing value measured (partial refresh 880 ms, full refresh 3374 ms) and
every current value flagged as *assumed* with a confidence label.

Key finding: 3 months is **not guaranteed** at 1-minute refresh. The gap
between the plausible current range (4–40 mA refresh, 3–50 µA idle) maps to
37–335 days of battery life. The single highest-leverage change is slower
refresh (5–10 minutes), which clears 3 months across the whole range and is a
one-line constant change.

The analysis also noted a missing design decision: the firmware does zero
daytime full refreshes, yet the ghosting test only validated 53 consecutive
partials — someone should deliberately decide on a periodic full refresh
rather than inheriting "none" by omission.

Document written to `docs/research/power-budget-analysis.md` with a runnable
Python model at the end.

### 2. Font size: 82pt → 88pt

While reading the font glyph table in `FontChango82.h` for the power analysis,
I noticed the xAdvance "squish" from Phase 5 (a uniform −8px on every
glyph's xAdvance, applied to fix `00:00` overflow) had left the 82pt font
at 275px for the widest 12-hour string — 21px below the 296px panel width.
The docs (`FONT_GENERATION.md`) still quoted the pre-squish "293px, 1px
margin" as the hard ceiling, meaning the squish had secretly freed ~18px of
headroom nobody knew about.

`font_tool.py scan` (project's own tool) confirmed:
- 82pt: 293px width shown by docs was correct pre-squish
- 84pt: overflows at 301px even post-squish
- 86pt: overflows at 308px
- 88pt: raw width 320px, squished to 290px → 6px margin on widest string

So 88pt is the clean ceiling with the −8px squish recipe. Generated
`FontChango88.h`, applied squish (xAdvance = width − 8), ran `fix` to
normalise yOffsets to −90 (mode) and centre the colon at −88 (digit midpoint:
−58.0, colon midpoint: −58.0 — exactly aligned).

Sprite sheet saved to `sprite_Chango88.png` for visual review before
deployment. Swapped includes and updated `setCursor` baseline to 123.

**Updated `FONT_GENERATION.md`** with the true post-squish widths for 82pt
and the real ceiling at 88pt.

| pt | Raw widest (10:00) | Squished widest (10:44) | Margin | Ink height | V-fill |
|----|-------------------|----------------------:|-------:|-----------:|------:|
| 82 | 293px | 275px | 21px | 62px | 63% |
| 88 | 320px | 290px | 6px | 67px | 68% |

### 3. MAC address on sync screen

The `STATE_SYNCING`/`STATE_RESYNCING` screen needed the BLE MAC address
displayed so users know what to configure in Home Assistant's
`mac_addresses` list.

**First attempt:** `HCI.localAddr` — this field is never populated on the
Cordio BLE stack (mbed core). Every byte stays 0x00.

**Root cause:** `HCI.localAddr` is only written when `readBdAddr()` is
called, and the Cordio stack doesn't call it during init. The `BLE.address()`
method does call `readBdAddr()` internally.

**Fix:** Used `BLE.address()` which sends the HCI `READ_BD_ADDR` command
and returns the controller's real MAC as a String.

**Second bug:** The first `drawClockFace()` in `setup()` runs *before*
`BLE.begin()`, so the MAC is all zeros on the first draw. Added a second
`drawClockFace()` after BLE initialises.

### 4. "Syncing..." font size

The `display.setFont(&FreeSansBold24pt7b)` call was lost from the
`STATE_SYNCING` case during indentation fix-ups in an earlier patch. The
NO_TIME case still had it, so "No Time!" was bold while "Syncing..." was
whatever font state leaked through (FreeSans9pt7b). Restored the call.

### 5. "Press any button to sync" clipping

`display.setCursor(0, display.height())` placed the text baseline right on
the panel edge. FreeSans9pt7b's descenders (g, y, j) extend +5px below the
baseline, clipping off the bottom. Fixed to use the same convention as the
rest of the code: `display.height() - 2 - 5`.

### 6. Stay-awake change (from last session, confirmed working)

Changed the button wake cooldown from the old 15-minute `WAKE_COOLDOWN` to
stay awake until the next 11pm shutdown. The comparison was also hardened:
`now >= g_awake_until` was changed to `(int32_t)(now - g_awake_until) >= 0`
to handle millis() wraparound (~49.7 days), since `g_awake_until` is now set
up to ~24 hours out.

## Challenges Encountered

1. Power model had no real measurements after three months of development —
   the project's highest risk was unaddressed.
2. `HCI.localAddr` is not populated on Cordio — silently returned zeros.
   Required reading the BLE stack source to find `BLE.address()`.
3. Multiple rounds of `patch` tool fighting indentation after a stack of
   incremental changes. The file accumulated inconsistent indentation that
   had to be cleaned up with Python.
4. The docs had stale numbers for font widths (pre-squish) that silently
   understated the actual headroom.

## What to measure next

1. Full-board idle current in WFE sleep (panel powered).
2. Average current during one partial refresh.
3. SSD1680 idle/standby current with the rail on.

These three measurements collapse the 37–335 day range to a single number.
See `docs/research/power-budget-analysis.md` for the full methodology.

## Next Steps

- Decide on periodic daytime full refresh for ghosting headroom (hourly
  vs never vs something else).
- Characterise actual power consumption on the bench (PPK2 or multimeter).
- Begin Phase 7: Enclosure design.
- Eventually: decide whether 5-minute or 1-minute refresh for battery life.

## Files Changed

- `firmware/src/main.cpp` — MAC address display, font swap, text clipping
  fix, syncing font fix, stay-awake sleep, redraw after BLE init
- `firmware/src/FontChango88.h` — **new** 88pt Chango font header
- `docs/research/power-budget-analysis.md` — **new** full power analysis
- `docs/lessons/2026-08-25-phase6-power-analysis-and-polish.md` — this file
- `docs/STATUS.md` — phase and status updated
- `docs/reference/FONT_GENERATION.md` — updated with correct post-squish
  widths and 88pt ceiling
- `sprite_Chango88.png` — **new** 88pt font sprite sheet
- `sprite_Chango-Regular_82pt.png` — **new** 82pt font sprite sheet (from
  previous session's font fix)

> **Note (2026-08-29):** the `sprite_*.png` files were one-off *visual-review* outputs
> of `icon_tool.py`/`font_tool.py` — never consumed by the build, docs, or any tool.
> They were removed from the repo as unreferenced artifacts. The sprite sheet is still
> regenerable on demand with `firmware/tools/icon_tool.py sprite ...`.
