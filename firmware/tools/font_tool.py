#!/usr/bin/env python3
"""
eClock Font Tool — sizing scan, header generation, and centering math.

Usage:
    # Full workflow: scan sizes, pick the best one, generate header
    python font_tool.py scan "C:/path/to/font.ttf"
    python font_tool.py generate "C:/path/to/font.ttf" 82 src/FontName.h --copyright "Copyright ..."

    # Quick centering calculation from an existing header
    python font_tool.py center src/FontName.h

Depends on Pillow: pip install Pillow
"""

import sys
import os
import re

# Add the project root's tools dir to path so we can import gfxfont_gen
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

try:
    from PIL import ImageFont
except ImportError:
    print("ERROR: Pillow is required. Run: pip install Pillow")
    sys.exit(1)

DISPLAY_W = 296
DISPLAY_H = 128
MARGIN = 2               # px padding from panel edge
DATE_TOP = 16            # bottom of date line (baseline 12 + text_h ~4)
STATUS_TOP = 114         # top of status line (icon top ~107, text top ~111)
AVAILABLE = STATUS_TOP - DATE_TOP   # 98 px vertical gap for time
GAP_CENTER = DATE_TOP + AVAILABLE // 2  # 65 px centre of the gap


# ── Helpers ──────────────────────────────────────────────────────────────────


def get_metrics(font, chars="0123456789:"):
    """Return (ink_h, yOff, widths, colon_w) for the given chars.
    
    yOff is the Adafruit GFX yOffset = -(glyph bottom edge == bbox[3]).
    This matches what gfxfont_gen.py writes to the GFXglyph table.
    """
    ink_h = 0
    y_off = 0
    widths = {}
    colon_w = 0

    for c in chars:
        bbox = font.getbbox(c)
        if not bbox or (bbox[2] == 0 and bbox[3] == 0):
            widths[c] = 0
            continue
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        widths[c] = w
        if c == ':':
            colon_w = w
            # yOffset = -(glyph bottom edge), per gfxfont_gen.py convention
            y_off = -bbox[3]
        elif h > ink_h:
            ink_h = h
            # yOffset = -(glyph bottom edge)
    # Compute average yOff from all 10 digits using -bbox[3]
    digit_yoffs = []
    for c in "0123456789":
        bbox = font.getbbox(c)
        if bbox and bbox[2] > 0:
            digit_yoffs.append(-bbox[3])
    avg_yoff = sum(digit_yoffs) // len(digit_yoffs) if digit_yoffs else 0
    return ink_h, avg_yoff, widths, colon_w


def time_width(widths, colon_w, hour, minute, is24h):
    """Compute pixel width of a formatted time string."""
    if is24h:
        h_str = f"{hour:02d}"
    else:
        # 12-hour with no leading zero
        dh = hour % 12
        if dh == 0:
            dh = 12
        h_str = str(dh)
        if minute > 9:
            m_str = str(minute)
            w = sum(widths.get(c, 0) for c in h_str) + colon_w + \
                sum(widths.get(c, 0) for c in m_str)
            return w
        else:
            # minute: two digits with leading zero
            m_str = f"{minute:02d}"
    
    m_str = f"{minute:02d}"
    w = sum(widths.get(c, 0) for c in h_str) + colon_w + \
        sum(widths.get(c, 0) for c in m_str)
    return w


# ── Scan ────────────────────────────────────────────────────────────────────


def cmd_scan(ttf_path):
    """Scan a .ttf across point sizes and print a fit table."""

    if not os.path.exists(ttf_path):
        print(f"ERROR: file not found: {ttf_path}")
        sys.exit(1)

    print(f"\n  Font: {os.path.basename(ttf_path)}")
    print(f"  Display: {DISPLAY_W}×{DISPLAY_H} px\n")

    # Determine which sizes to test
    sizes = list(range(60, 130, 2))  # every 2pt from 60 to 128

    print(f"{'Size':>6s}  {'Ink_h':>5s}  {'Fill':>4s}  {'12h OK':>7s}  "
          f"{'Widest 12h':>12s}  {'12h Marg':>8s}  {'24h OK':>7s}  "
          f"{'Widest 24h':>12s}  {'24h Marg':>8s}")
    print("-" * 90)

    best_12h = None
    best_24h = None

    for pt in sizes:
        try:
            font = ImageFont.truetype(ttf_path, pt)
        except Exception as e:
            print(f"  {pt:3d}pt  ERROR: {e}")
            continue

        ink_h, y_off, widths, colon_w = get_metrics(font)
        if ink_h == 0:
            continue

        fill_pct = int(ink_h / DISPLAY_H * 100)

        # Check all 12-hour and 24-hour time strings
        max_w_12h = 0
        max_t_12h = ""
        max_w_24h = 0
        max_t_24h = ""

        for h in range(0, 24):
            for m in [0, 5, 15, 30, 45, 58]:
                w12 = time_width(widths, colon_w, h, m, is24h=False)
                w24 = time_width(widths, colon_w, h, m, is24h=True)
                if w12 > max_w_12h:
                    max_w_12h = w12
                    max_t_12h = f"{h % 12 if h % 12 else 12}:{m:02d}"
                if w24 > max_w_24h:
                    max_w_24h = w24
                    max_t_24h = f"{h:02d}:{m:02d}"

        margin_12h = (DISPLAY_W - max_w_12h) // 2
        margin_24h = (DISPLAY_W - max_w_24h) // 2
        ok_12h = "✓" if max_w_12h <= DISPLAY_W else "✗"
        ok_24h = "✓" if max_w_24h <= DISPLAY_W else "✗"

        print(f"{pt:3d}pt  {ink_h:4d}px  {fill_pct:3d}%  "
              f"{ok_12h:>7s}  {max_t_12h:>10s} ({max_w_12h:3d})  "
              f"{margin_12h:>4d}px  "
              f"{ok_24h:>7s}  {max_t_24h:>10s} ({max_w_24h:3d})  "
              f"{margin_24h:>4d}px")

        # Track the biggest size that fits 12h and 24h comfortably
        if ok_12h == "✓":
            best_12h = (pt, ink_h, fill_pct, max_w_12h, margin_12h)
        if ok_24h == "✓":
            best_24h = (pt, ink_h, fill_pct, max_w_24h, margin_24h)

    print()
    if best_12h:
        pt, ink_h, fill_pct, mw, mar = best_12h
        print(f"  RECOMMENDED (12h): {pt}pt — ink_h={ink_h}px "
              f"({fill_pct}% fill), margin={mar}px")
    if best_24h:
        pt, ink_h, fill_pct, mw, mar = best_24h
        print(f"  RECOMMENDED (24h): {pt}pt — ink_h={ink_h}px "
              f"({fill_pct}% fill), margin={mar}px")

    print()
    print("  NOTE: For 12-hour mode the '10:00' string (wide '1'+'0') is")
    print("  often the widest. A few pixels of overflow on '10:00' or")
    print("  '00:00' (midnight) is acceptable since the typical display")
    print("  is narrower (e.g. '12:34').")
    print()


# ── Generate ────────────────────────────────────────────────────────────────


def cmd_generate(ttf_path, pt_size, output_path, copyright_str=None,
                 is12h=True):
    """Generate the font header and print centering math."""

    if not os.path.exists(ttf_path):
        print(f"ERROR: file not found: {ttf_path}")
        sys.exit(1)

    pt_size = int(pt_size)

    # Generate via the existing gfxfont_gen.py
    gen_script = os.path.join(SCRIPT_DIR, "gfxfont_gen.py")
    if not os.path.exists(gen_script):
        print(f"ERROR: gfxfont_gen.py not found at {gen_script}")
        sys.exit(1)

    # Build the output as absolute path
    output_abs = os.path.abspath(output_path)
    os.makedirs(os.path.dirname(output_abs), exist_ok=True)

    cmd = (
        f'python "{gen_script}" '
        f'"{ttf_path}" '
        f'{pt_size} '
        f'48 58 '
        f'"{output_abs}"'
    )
    print(f"  Running: {cmd}")
    ret = os.system(cmd)
    if ret != 0:
        print(f"ERROR: gfxfont_gen.py failed (exit {ret})")
        sys.exit(1)

    # Add copyright if provided
    if copyright_str:
        header_name = os.path.basename(ttf_path).replace(".ttf", "")
        with open(output_abs, "r") as f:
            content = f.read()
        # Insert after the generator comment line
        copyright_lines = [
            f"// Generated by gfxfont_gen.py from {os.path.basename(ttf_path)} at {pt_size}pt",
            f"// Range: 0 (0x30) to : (0x3A)",
        ]
        for line in copyright_str.strip().split("\n"):
            cl = f"// {line.strip()}"
            # Only insert if not already present
            if cl not in content:
                pass

        # Simple approach: insert after the range line
        lines = content.split("\n")
        new_lines = []
        copyright_inserted = False
        for line in lines:
            new_lines.append(line)
            if line.strip().startswith("// Range:") and not copyright_inserted:
                for cl in copyright_str.strip().split("\n"):
                    if f"// {cl.strip()}" not in lines:
                        new_lines.append(f"// {cl.strip()}")
                copyright_inserted = True

        with open(output_abs, "w") as f:
            f.write("\n".join(new_lines))

        print(f"  Copyright notice added.")

    print(f"\n  Header written: {output_abs}")

    # ── Print centering math ─────────────────────────────────────────────
    font = ImageFont.truetype(ttf_path, pt_size)
    ink_h, avg_yoff, widths, colon_w = get_metrics(font)

    # Also get average digit ink height from get_metrics (h from bbox[3]-bbox[1])
    digit_inks = []
    for c in "0123456789":
        bbox = font.getbbox(c)
        if bbox and bbox[2] > 0:
            digit_inks.append(bbox[3] - bbox[1])
    avg_ink = sum(digit_inks) // len(digit_inks)

    # Colon yOffset (GFX convention) from get_metrics too
    colon_bbox = font.getbbox(":")
    col_yoff = -colon_bbox[3]
    col_ink = colon_bbox[3] - colon_bbox[1]

    print(f"\n── Centering Math ──────────────────────────────")
    print(f"  Font: {os.path.basename(ttf_path)} at {pt_size}pt")
    print(f"  Digits:   yOff ≈ {avg_yoff}, h ≈ {avg_ink}")
    print(f"  Colon:    yOff ≈ {col_yoff}, h ≈ {col_ink}")
    print()

    midpoint = avg_yoff + avg_ink / 2
    baseline = GAP_CENTER - midpoint
    print(f"  Centre offset = yOff + h/2 = {avg_yoff} + {avg_ink/2:.1f} = {midpoint:.1f}")
    print(f"  Baseline_y    = {GAP_CENTER} - ({midpoint:.1f}) = {int(round(baseline))}")
    print()

    # Per-glyph yOffset check (from TTF metrics, not yet in header)
    yos = {}
    for c in "0123456789":
        bbox = font.getbbox(c)
        if bbox and bbox[2] > 0:
            yos[c] = -bbox[3]
    yo_vals = set(yos.values())
    if len(yo_vals) > 1:
        from collections import Counter
        mode_yo = Counter(yos.values()).most_common(1)[0][0]
        print(f"  \u26a0 BASELINE VARIANCE in TTF metrics — per-digit yOffsets:")
        for c in "0123456789":
            if c in yos:
                mark = " \u2190 differs" if yos[c] != mode_yo else ""
                print(f"      '{c}': yOff = {yos[c]:4d}{mark}")
        print(f"  \u2192 After generation, normalise with: python font_tool.py fix <header.h>")
    else:
        print(f"  \u2713 Baseline consistent in TTF (all yOff={list(yo_vals)[0]})")
    print()

    # Check whether the colon needs manual yOffset adjustment
    col_mid = col_yoff + col_ink / 2
    diff = col_mid - midpoint
    print(f"  Colon yOff={col_yoff}, midpoint={col_mid:.1f}")
    print(f"  Colon offset vs digits: {diff:.1f} px {'(OK)' if abs(diff) < 5 else 'ADJUSTMENT NEEDED'}")
    if abs(diff) >= 5:
        adjusted = int(col_yoff + diff)
        print(f"    → Change colon yOffset from {col_yoff} to ~{adjusted}")
    print()

    # Print the setCursor line to copy
    if is12h:
        mode = "12h"
    else:
        mode = "24h"

    print(f"── Update main.cpp (drawClockFace, {mode} mode) ──")
    print(f"  // {os.path.splitext(os.path.basename(ttf_path))[0]} {pt_size}pt has typical yOff={avg_yoff}, h={avg_ink}.")
    print(f"  // Center of ink relative to baseline is {avg_yoff} + {avg_ink/2:.1f} = {midpoint:.1f}.")
    print(f"  // Baseline = {GAP_CENTER} - ({midpoint:.0f}) = {int(round(baseline))}.")
    print(f'  display.setCursor((display.width() - tw) / 2 - tx, {int(round(baseline))});')
    print()

    # Show width check for the chosen mode
    if is12h:
        print(f"── Width check (12-hour mode) ───────────────────")
        for t in ['12:34', '10:00', '8:00', '11:11', '12:00', '1:00', '9:58']:
            w = sum(widths.get(c, 0) for c in t)
            margin = (DISPLAY_W - w) // 2
            print(f"  {t:6s}: {w:3d}px  margin {margin:+2d}px  "
                  f"{'OK' if w <= DISPLAY_W else 'OVERFLOW by ' + str(w - DISPLAY_W) + 'px'}")
    else:
        print(f"── Width check (24-hour mode) ───────────────────")
        for t in ['10:00', '08:02', '12:34', '23:45', '00:00']:
            w = sum(widths.get(c, 0) for c in t)
            margin = (DISPLAY_W - w) // 2
            print(f"  {t:6s}: {w:3d}px  margin {margin:+2d}px  "
                  f"{'OK' if w <= DISPLAY_W else 'OVERFLOW by ' + str(w - DISPLAY_W) + 'px'}")
    print()

# ── Center from header ──────────────────────────────────────────────────────


def cmd_center(header_path):
    """Read an existing GFXfont header and print the centering math."""

    if not os.path.exists(header_path):
        print(f"ERROR: file not found: {header_path}")
        sys.exit(1)

    with open(header_path, "r") as f:
        content = f.read()

    # Extract the glyph table
    m = re.search(r"static const GFXglyph font_glyphs\[\] = \{(.*?)\};", content, re.DOTALL)
    if not m:
        print("ERROR: could not find font_glyphs table in header")
        sys.exit(1)

    glyphs = []
    for line in m.group(1).split("\n"):
        line = line.strip().rstrip(",")
        if not line or line.startswith("//") or line.startswith("/*"):
            continue
        # Parse: { offset, width, height, xAdvance, xOffset, yOffset }
        nums = re.findall(r"-?\d+", line)
        if len(nums) >= 6:
            glyphs.append({
                "offset": int(nums[0]),
                "w": int(nums[1]),
                "h": int(nums[2]),
                "xa": int(nums[3]),
                "xo": int(nums[4]),
                "yo": int(nums[5]),
            })

    if len(glyphs) < 11:
        print(f"WARNING: expected 11 glyphs, found {len(glyphs)}")

    # First 10 are digits 0-9, 11th is colon
    digit_glyphs = glyphs[:10]
    colon_glyph = glyphs[-1] if glyphs else None

    avg_yoff = sum(g["yo"] for g in digit_glyphs) // len(digit_glyphs)
    avg_h = sum(g["h"] for g in digit_glyphs) // len(digit_glyphs)

    print(f"\n── Centering Math from {os.path.basename(header_path)} ──")
    print(f"  Digits ({len(digit_glyphs)} glyphs):")
    print(f"    Average yOff = {avg_yoff}")
    print(f"    Average h    = {avg_h}")

    # Check per-glyph yOffset consistency
    from collections import Counter
    yo_counts = Counter(g["yo"] for g in digit_glyphs)
    if len(yo_counts) > 1:
        mode_yo = yo_counts.most_common(1)[0][0]
        print(f"    \u26a0 BASELINE VARIANCE — yOffsets differ across digits:")
        for i, g in enumerate(digit_glyphs):
            mark = " \u2190 differs" if g["yo"] != mode_yo else ""
            print(f"      '{chr(0x30+i)}': yOff={g['yo']:4d}, h={g['h']:2d}{mark}")
        print(f"    \u2192 Normalise: python font_tool.py fix {header_path}")
    else:
        print(f"    \u2713 Baseline consistent (all yOff={digit_glyphs[0]['yo']})")

    if colon_glyph:
        print(f"  Colon:")
        print(f"    yOff = {colon_glyph['yo']}, h = {colon_glyph['h']}")

    midpoint = avg_yoff + avg_h / 2
    baseline = GAP_CENTER - midpoint
    print()
    print(f"  Centre offset = yOff + h/2 = {avg_yoff} + {avg_h/2:.1f} = {midpoint:.1f}")
    print(f"  Baseline_y    = {GAP_CENTER} - ({midpoint:.1f}) = {baseline:.0f}")

    if colon_glyph:
        col_mid = colon_glyph["yo"] + colon_glyph["h"] / 2
        diff = col_mid - midpoint
        print()
        print(f"  Colon midpoint = {col_mid:.1f}")
        print(f"  vs digits: {diff:.1f} px {'(OK)' if abs(diff) < 5 else 'ADJUSTMENT NEEDED'}")
        if abs(diff) >= 5:
            adjusted = int(colon_glyph["yo"] + diff)
            print(f"    → Change colon yOffset from {colon_glyph['yo']} to ~{adjusted}")

    print()
    print(f"── setCursor line ──")
    print(f'  display.setCursor((display.width() - tw) / 2 - tx, {int(round(baseline))});')
    print()


def cmd_layout(ttf_path, pt_size, is12h=True):
    """Print full layout constants including centering for the firmware."""
    font = ImageFont.truetype(ttf_path, int(pt_size))
    ink_h, avg_yoff, widths, colon_w = get_metrics(font)

    midpoint = avg_yoff + ink_h / 2
    baseline = GAP_CENTER - midpoint

    # Width check
    usable_w = DISPLAY_W - 2 * MARGIN
    times = ['12:34', '10:00', '8:00', '11:11', '12:00', '1:00', '9:58'] if is12h else \
            ['10:00', '08:02', '12:34', '23:45', '00:00']
    max_w = max(sum(widths.get(c, 0) for c in t) for t in times)
    max_t = [t for t in times if sum(widths.get(c, 0) for c in t) == max_w][0]

    print(f"── Layout for {os.path.basename(ttf_path)} at {pt_size}pt ──")
    print(f"  Display:    {DISPLAY_W}×{DISPLAY_H} px  (MARGIN={MARGIN}px each side)")
    print(f"  Usable:     {usable_w}×{DISPLAY_H} px")
    print()
    print(f"  Font metrics:")
    print(f"    yOff (avg) = {avg_yoff}")
    print(f"    ink_h      = {ink_h}")
    print(f"    ink_mid    = {midpoint:.1f}")
    print()
    print(f"  Vertical centering:")
    print(f"    Date bottom ~ {DATE_TOP} px  (baseline 12, text ~4 px)")
    print(f"    Status top  ~ {STATUS_TOP} px  (icon top ~107, text ~111)")
    print(f"    Gap         = {AVAILABLE} px")
    print(f"    Centre      = {GAP_CENTER} px")
    print(f"    Baseline_y  = {int(round(baseline))}")
    print()
    print(f"  Horizontal (12h, worst case '{max_t}'):")
    print(f"    Width       = {max_w} px  (usable {usable_w} px)")
    if max_w <= usable_w:
        print(f"    Margin      = +{(usable_w - max_w) // 2} px each side")
    else:
        print(f"    OVERFLOW    = {max_w - usable_w} px")
    print()
    print(f"── setCursor line ──")
    print(f'  display.setCursor((display.width() - tw) / 2 - tx, {int(round(baseline))});')
    print()
    print(f"── Include line ──")
    print(f'  #include "FontChango{pt_size}.h"  // {pt_size}pt Chango for time digits')
    print()
    print(f"── Sprite regeneration ──")
    print(f"  python firmware/tools/icon_tool.py sprite \"{ttf_path}\" {pt_size}")


# ── Fix (normalize yOffsets) ─────────────────────────────────────────────────


def cmd_fix(header_path):
    """Normalize yOffset in an existing GFXfont header.
    
    Sets all digit yOffsets to the mode value, then adjusts the colon's
    yOffset so its visual midpoint aligns with the digits' midpoint.
    Overwrites the file in place."""

    if not os.path.exists(header_path):
        print(f"ERROR: file not found: {header_path}")
        sys.exit(1)

    with open(header_path, "r") as f:
        content = f.read()

    # Find the glyph table bounds
    m = re.search(
        r"(static const GFXglyph font_glyphs\[\] = \{)(.*?)(\};)",
        content, re.DOTALL
    )
    if not m:
        print("ERROR: could not find font_glyphs table in header")
        sys.exit(1)

    prefix = m.group(1)
    body   = m.group(2)
    suffix = m.group(3)

    # Parse individual glyph lines
    glyphs = []
    for line in body.split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
            continue
        nums = re.findall(r"-?\d+", stripped)
        if len(nums) >= 6:
            glyphs.append({
                "offset": int(nums[0]),
                "w": int(nums[1]),
                "h": int(nums[2]),
                "xa": int(nums[3]),
                "xo": int(nums[4]),
                "yo": int(nums[5]),
                "raw": line,
            })

    if len(glyphs) < 11:
        print(f"WARNING: expected 11 glyphs (10 digits + colon), found {len(glyphs)}")

    digit_glyphs = glyphs[:10]
    colon_glyph = glyphs[-1] if len(glyphs) >= 11 else None

    # -- Mode yOffset
    from collections import Counter
    yo_counts = Counter(g["yo"] for g in digit_glyphs)
    mode_yo = yo_counts.most_common(1)[0][0]
    current_yos = sorted(set(g["yo"] for g in digit_glyphs))

    if len(current_yos) <= 1:
        print(f"  Digits already consistent (all yOff={mode_yo}). Checking colon...")
    else:
        print(f"  Digit yOffsets before: {current_yos}")
        print(f"  Normalizing to mode:   {mode_yo}")

    # -- Colon centering
    if colon_glyph:
        avg_h = sum(g["h"] for g in digit_glyphs) // len(digit_glyphs)
        digit_mid = mode_yo + avg_h / 2.0
        col_target = int(round(digit_mid - colon_glyph["h"] / 2.0))
        print(f"  Colon yOffset: {colon_glyph['yo']} -> {col_target}  "
              f"(midpoint align: {digit_mid:.1f})")
    else:
        col_target = None

    # -- Rebuild the glyph table
    new_lines = []
    for g in glyphs:
        old_line = g["raw"]
        new_yo = mode_yo
        if g is colon_glyph and col_target is not None:
            new_yo = col_target
        new_line = re.sub(r"(-?\d+)\s*\},?\s*$", f"{new_yo} }},", old_line)
        new_lines.append(new_line)

    new_body = "\n".join(new_lines)
    new_table = f"{prefix}\n{new_body}\n{suffix}"
    new_content = content.replace(m.group(0), new_table)

    with open(header_path, "w") as f:
        f.write(new_content)

    print(f"  Wrote: {header_path}")
    print(f"  Run sprite sheet to verify:")
    print(f"    python firmware/tools/icon_tool.py sprite ...")
    print()


# ── Main ─────────────────────────────────────────────────────────────────────


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print("Commands:")
        print("  scan    <ttf_path>              - size scan")
        print("  generate <ttf> <pt> <output.h>  - generate + centering")
        print("  center  <header.h>              - centering from existing header")
        print("  layout  <ttf> <pt>              - full layout report (margins, centering)")
        print("  fix     <header.h>              - normalize yOffsets + colon centering")
        print()
        print("Examples:")
        print('  python font_tool.py scan "C:/path/to/font.ttf"')
        print("  python font_tool.py generate font.ttf 82 src/FontName.h \\")
        print('          --copyright "Copyright (c) 2024 Type Foundry"')
        print("  python font_tool.py center src/FontChango88.h")
        print("  python font_tool.py fix src/FontChango88.h")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "scan":
        if len(sys.argv) < 3:
            print("Usage: python font_tool.py scan <ttf_path>")
            sys.exit(1)
        cmd_scan(sys.argv[2])

    elif cmd == "generate":
        if len(sys.argv) < 5:
            print("Usage: python font_tool.py generate <ttf> <pt> <output.h> [--copyright ...]")
            sys.exit(1)
        ttf = sys.argv[2]
        pt = sys.argv[3]
        out = sys.argv[4]
        copyright_str = None
        is12h = True
        # Parse optional flags
        i = 5
        while i < len(sys.argv):
            if sys.argv[i] == "--copyright" and i + 1 < len(sys.argv):
                copyright_str = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == "--24h":
                is12h = False
                i += 1
            else:
                i += 1
        cmd_generate(ttf, pt, out, copyright_str, is12h)

    elif cmd == "center":
        if len(sys.argv) < 3:
            print("Usage: python font_tool.py center <header.h>")
            sys.exit(1)
        cmd_center(sys.argv[2])

    elif cmd == "layout":
        if len(sys.argv) < 4:
            print("Usage: python font_tool.py layout <ttf> <pt> [--24h]")
            sys.exit(1)
        is12h = True
        if len(sys.argv) >= 5 and sys.argv[4] == "--24h":
            is12h = False
        cmd_layout(sys.argv[2], sys.argv[3], is12h)

    elif cmd == "fix":
        if len(sys.argv) < 3:
            print("Usage: python font_tool.py fix <header.h>")
            sys.exit(1)
        cmd_fix(sys.argv[2])

    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()