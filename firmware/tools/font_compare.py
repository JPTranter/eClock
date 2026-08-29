#!/usr/bin/env python3
"""Compare display-font candidates for the eClock time digits.

Measures each candidate TTF against the actual 296x128 panel geometry: for a given
point size it fits the WORST-CASE time string within the 296px width (applying the
same -8px-per-glyph xAdvance squish the shipped Chango font uses), reports the
vertical fill, and renders a labeled comparison sheet.

Usage:
    python font_compare.py <fonts_dir> [--output <png>] [--time "10:44"]
    python font_compare.py "C:/Users/jptra/AppData/Local/Temp/fontcmp"

The worst-case 12h string is 10:44 (double-digit hour '10' + the wide '4's).
The squish is the shipped recipe: xAdvance = glyph_width - 8 per glyph.

Requires Pillow.
"""
from __future__ import annotations

import argparse
import os
import sys
from PIL import Image, ImageDraw, ImageFont

PANEL_W, PANEL_H = 296, 128
SQUISH = 8            # xAdvance = glyph_width - 8  (the shipped recipe, see LESSONS #20)
TIME_STR = "10:44"    # worst-case 12h string (also measured; the comparer scans a few)

# (name, fitted point size) — sizes chosen so the squished worst-case width <= 296.
# Update these if a font fails to fit (the script reports the true margin).
CANDIDATES = [
    ("passion", 149), ("chewy", 122), ("luckiest", 117), ("lilita", 115),
    ("passionblack", 125), ("lemon", 96), ("titanone", 102), ("rammetto", 85),
    ("shrikhand", 97), ("chango", 84), ("delagothic", 83), ("poller", 81),
    ("sigmar", 97), ("seymour", 72), ("modak", 123), ("sniglet", 114),
]


def ink_metrics(font, text: str):
    """Return (ink_w, ink_h, per_glyph_widths) for text rendered at a PIL font."""
    bbox = font.getbbox(text)
    if bbox is None:
        return 0, 0, []
    x0, y0, x1, y1 = bbox
    widths = []
    for ch in text:
        cb = font.getbbox(ch)
        widths.append(cb[2] - cb[0] if cb else 0)
    return (x1 - x0), (y1 - y0), widths


def squished_width(font, text: str) -> int:
    """Width of text under the shipped squish: xAdvance = glyph_w - 8 per glyph."""
    total = 0
    for ch in text:
        cb = font.getbbox(ch)
        if cb:
            w = cb[2] - cb[0]
            total += max(0, w - SQUISH)
    return total


def fits(font, text: str) -> bool:
    return squished_width(font, text) <= PANEL_W


def measure(name: str, pt: int, fonts_dir: str, ttf_name: str):
    path = os.path.join(fonts_dir, ttf_name)
    if not os.path.exists(path):
        return None
    f = ImageFont.truetype(path, pt)
    worst = max(fits(f, t) for t in ["10:44", "12:58", "00:00", "08:38"])
    ink_w, ink_h, _ = ink_metrics(f, TIME_STR)
    w = squished_width(f, TIME_STR)
    margin = PANEL_W - w
    fill = round(ink_h / PANEL_H * 100)
    return {"name": name, "pt": pt, "ink_h": ink_h, "fill": fill,
            "width": w, "margin": margin, "worst_fits": worst}


def render_cell(name: str, pt: int, fonts_dir: str, ttf_name: str,
                m: dict | None, bg=(0xD8, 0xD8, 0xD8)):
    cell = Image.new("RGB", (PANEL_W, PANEL_H), bg)
    if m is None:
        return cell, "download missing"
    d = ImageDraw.Draw(cell)
    f = ImageFont.truetype(os.path.join(fonts_dir, ttf_name), pt)
    # centre the string horizontally, baseline roughly mid-panel
    bb = f.getbbox(TIME_STR)
    w = bb[2] - bb[0]; h = bb[3] - bb[1]
    x = (PANEL_W - w) // 2 - bb[0]
    y = (PANEL_H - h) // 2 - bb[1]
    d.text((x, y), TIME_STR, font=f, fill=0)
    label = f"{name} {pt}pt  {m['fill']}%  m{m['margin']:+d}px"
    return cell, label


def build_sheet(fonts_dir: str, out: str, cols: int = 2):
    bg = (0xD8, 0xD8, 0xD8)
    label_h, gap = 18, 8
    ttf = {name: name + ".ttf" for name, _ in CANDIDATES}
    # Chango ships in the repo; the rest are downloaded. Chango's repo name differs.
    ttf["chango"] = "chango.ttf"
    measured = []
    for name, pt in CANDIDATES:
        m = measure(name, pt, fonts_dir, ttf[name])
        measured.append((name, pt, ttf[name], m))

    rows = (len(measured) + cols - 1) // cols
    sheet_w = cols * PANEL_W + gap * (cols + 1)
    sheet_h = 30 + rows * (PANEL_H + label_h) + gap * (rows + 1)
    sheet = Image.new("RGB", (sheet_w, sheet_h), bg)
    d = ImageDraw.Draw(sheet)
    # title
    d.text((gap, 6), f"eClock time-font candidates ({TIME_STR}, squished, 296px panel)",
           fill=0)
    y = 30
    for i, (name, pt, ttf_name, m) in enumerate(measured):
        cell, label = render_cell(name, pt, fonts_dir, ttf_name, m, bg)
        col = i % cols; row = i // cols
        x = gap + col * (PANEL_W + gap)
        cy = y + row * (PANEL_H + label_h + gap)
        sheet.paste(cell, (x, cy))
        d.text((x + 2, cy + PANEL_H + 2), label, fill=0)
    sheet.save(out)
    print(f"wrote {out} ({sheet.size})")
    print("name           pt   fill%  width  margin  worst-fit")
    for name, pt, ttf_name, m in measured:
        if m:
            print(f"{name:14s} {pt:4d} {m['fill']:5d}  {m['width']:5d} {m['margin']:+5d}  "
                  f"{'YES' if m['worst_fits'] else 'NO <-- overflows'}")
        else:
            print(f"{name:14s} {pt:4d}  MISSING {ttf_name}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir")
    ap.add_argument("--output", default="docs/reference/candidate_fonts_comparison.png")
    ap.add_argument("--cols", type=int, default=2)
    args = ap.parse_args()
    build_sheet(args.fonts_dir, args.output, args.cols)


if __name__ == "__main__":
    main()
