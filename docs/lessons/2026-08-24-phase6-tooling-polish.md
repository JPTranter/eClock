# 2026-08-24: Phase 6 Tooling Polish & Baseline Fix

## Summary

Three changes led by user observation that time digits had uneven baselines.
We normalised the font, hardened the tooling so this never recurs, and fixed
the sprite sheet to work with display fonts.

## Changes

### 1. FontChango82.h — yOffset normalization

All 10 digit glyphs now share yOffset=-84 (mode value). Colon centred to -82
(midpoint alignment with digits). Previously '1','2','4' were at -82 (2px low),
'7' at -83 (1px low), colon at -83 (1px off-centre).

### 2. font_tool.py — baseline variance detection + `fix` command

- `generate`: now warns if the TTF has per-digit yOffset variance before header
  is written
- `center`: now prints per-glyph yOffsets with `← differs` marker
- `fix <header.h>`: new command that normalises all digit yOffsets to mode value
  and centres the colon relative to the digit midpoint

### 3. icon_tool.py — auto-sizing sprite cells

`sprite` command had hardcoded `cell_h=24` and `icon_area_w=60` — designed for
14-20pt Material Icons. Now computes `max_ink_w`/`max_ink_h` from actual glyphs
and sizes cells dynamically. Works for both 20pt icons and 82pt display fonts.

### 4. Documentation

- FONT_GENERATION.md: Quickest path now includes `fix` step. New section on
  baseline variance across digits. Quick reference expanded with full workflow.
- LESSONS_LEARNT.md: Lesson 17 (baseline variance) and Lesson 18 (sprite cell
  sizing). Removed duplicated Phase 5 content.

### 5. Firmware deployed

Built and flashed to hardware. Build: 360KB flash (44.4%), 74KB RAM (31.5%).
