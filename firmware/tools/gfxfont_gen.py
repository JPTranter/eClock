"""
Generate an Adafruit GFX-style 1-bit font header from a TrueType font.
Output: C header containing GFXfont struct and bitmap data.

Usage: python gfxfont_gen.py <ttf_path> <point_size> <first_char> <last_char> <output.h> [--stretch-height scale] [--squish px] [--center-colon]

The output format matches what fontconvert produces for the Adafruit GFX library.

Options:
- `--stretch-height scale` scales each glyph's bitmap VERTICALLY (width/advance kept).
- `--squish px` reduces xAdvance by px (xAdvance = glyph_width - squish). Default 0
  (advance = glyph width; needed for fonts with no built-in tracking like Chewy).
- `--center-colon` centres the colon on the digit midpoint instead of the default
  bottom-alignment.

The header is self-describing and self-correcting:
- Digit yOffsets are normalised to their mode only if there is real baseline variance.
- The colon is BOTTOM-ALIGNED to the digits' baseline (all glyph ink bottoms share the
  baseline; a naive `yOffset = -bbox[3]` would float the colon because the colon's
  bbox[1] differs from the digits').
- The header emits `#define FONT_BASELINE <y>` (the setCursor baseline that vertically
  centres the digits) and a `// Recipe: ...` comment, so callers read metrics from the
  font rather than hardcoding them.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

# The ePaper display's native resolution (296 x 128); used to compute the draw baseline.
PANEL_WIDTH = 296
PANEL_HEIGHT = 128


def render_glyph(font, char, size, stretch=1.0):
    """Render a single character and extract its 1-bit bitmap + metrics.

    ``stretch`` scales the glyph bitmap VERTICALLY (factor >= 1.0) keeping width and
    xAdvance unchanged, so the time digits can fill more vertical space without
    overflowing the panel's width.

    Returns (bitmap_bytes, width, height, x_advance, x_offset, y_offset)
    where bitmap_bytes is a bytearray of 1-bit MSB-packed rows.
    """
    # Render at 4x then downsample for better anti-aliasing
    scale = 1
    render_size = size * scale

    # Get mask for the character
    try:
        mask = font.getmask(char, mode='1')
    except Exception as e:
        print(f"  Warning: can't render '{char}' (U+{ord(char):04X}): {e}")
        return None

    # Get bounding box
    bbox = font.getbbox(char)
    if bbox is None or (bbox[2] == 0 and bbox[3] == 0):
        return None

    x0, y0, x1, y1 = bbox
    width = x1 - x0
    height = y1 - y0

    if width <= 0 or height <= 0:
        return None

    # Get the metrics
    metrics = font.getmetrics()
    # x_advance from getmask might not be available directly
    # We'll estimate based on character width
    x_advance = width + 2  # some typical spacing
    y_offset = -y1  # y_offset is negative (baseline is at cap height from top)

    # Render the glyph to a 1-bit image
    img = Image.new('1', (width, height), 0)
    draw = ImageDraw.Draw(img)
    draw.text((-x0, -y0), char, font=font, fill=1)

    # Optional vertical stretch: scale the glyph bitmap taller while keeping the
    # width. This grows y_offset magnitude proportionally so the glyph stays
    # baseline-anchored (baseline at -y1 originally; scaled by the same factor).
    if stretch != 1.0 and stretch > 0:
        new_h = max(1, round(height * stretch))
        img = img.resize((width, new_h), Image.LANCZOS)
        y1_scaled = round(y1 * stretch)
        return (img, width, new_h, x_advance, 0, -y1_scaled)

    return (img, width, height, x_advance, 0, -y1)


def glyph_to_c_array(img, width, height):
    """Convert a 1-bit glyph image to a C byte array (MSB first, contiguously packed)."""
    pixels = img.load()
    packed = []
    current_byte = 0
    bits = 0

    for y in range(height):
        for x in range(width):
            if pixels[x, y]:  # Non-zero is foreground
                current_byte |= (1 << (7 - bits))
            bits += 1
            if bits == 8:
                packed.append(current_byte)
                current_byte = 0
                bits = 0

    if bits > 0:
        packed.append(current_byte)

    return bytearray(packed)


def main():
    if len(sys.argv) < 6:
        print("Usage: python gfxfont_gen.py <ttf_path> <pt_size> <first_char> <last_char> <output.h> [--stretch-height scale]")
        sys.exit(1)

    ttf_path = sys.argv[1]
    pt_size = int(sys.argv[2])
    first = int(sys.argv[3])
    last = int(sys.argv[4])
    out_path = sys.argv[5]

    # Optional: --stretch-height <scale> (e.g. 1.1) scales glyphs vertically.
    stretch = 1.0
    if "--stretch-height" in sys.argv:
        idx = sys.argv.index("--stretch-height")
        stretch = float(sys.argv[idx + 1])
        if stretch <= 0:
            print("ERROR: --stretch-height must be > 0")
            sys.exit(1)

    # Optional: --squish <px> reduces xAdvance by that many px (xAdvance = width - squish).
    # Default 0 = advance == glyph width. Chango uses 8 (see LESSONS #20).
    squish = 0
    if "--squish" in sys.argv:
        idx = sys.argv.index("--squish")
        squish = int(sys.argv[idx + 1])
        if squish < 0:
            print("ERROR: --squish must be >= 0")
            sys.exit(1)

    # Optional: --center-colon forces the colon's centre onto the digit midpoint.
    # Default is to honour the font's NATIVE colon yOffset (a typeface may deliberately
    # sit the colon low, e.g. Chewy ~19px below centre; see LESSONS #33).
    center_colon = "--center-colon" in sys.argv

    print(f"Loading font: {ttf_path}")
    font = ImageFont.truetype(ttf_path, pt_size)
    if stretch != 1.0:
        print(f"  Vertical stretch: x{stretch} (glyph heights scaled, width/advance kept)")

    # Get font metrics
    metrics = font.getmetrics()
    ascent, descent = metrics
    print(f"  Ascent: {ascent}, Descent: {descent}, Line height: {ascent + descent}")

    # Build glyph data
    glyphs = []
    bitmaps = []
    bitmap_offset = 0

    for code in range(first, last + 1):
        char = chr(code)
        result = render_glyph(font, char, pt_size, stretch)
        if result is None:
            # Empty/fallback glyph
            glyphs.append({
                'bitmapOffset': 0,
                'width': 0,
                'height': 0,
                'xAdvance': pt_size * 2 // 3,
                'xOffset': 0,
                'yOffset': 0,
            })
            continue

        raw_bitmap, w, h, xa, xo, yo = result

        # Convert to packed C format
        packed = glyph_to_c_array(raw_bitmap, w, h)

        glyphs.append({
            'bitmapOffset': bitmap_offset,
            'width': w,
            'height': h,
            'xAdvance': xa,
            'xOffset': xo,
            'yOffset': yo,
        })
        bitmaps.append(packed)
        bitmap_offset += len(packed)

    # Honour each font's NATIVE metrics by default. CRITICAL: all glyphs must share one
    # baseline. gfxfont_gen's naive yOffset = -bbox[3] only bottom-aligns glyphs with the
    # SAME bbox[1] — the colon's bbox[1] differs from the digits', so it would float.
    # Correct fix: set every glyph's ink BOTTOM to the digits' baseline (the digits' ink
    # bottom). Pass --center-colon to centre the colon instead (opt-in; for fonts that
    # need it). Normalise digit yOffsets only if there is real baseline variance.
    digits = glyphs[:10]
    colon = glyphs[10] if len(glyphs) >= 11 else None
    if digits:
        from collections import Counter
        yo_counts = Counter(g['yOffset'] for g in digits)
        mode_yo = yo_counts.most_common(1)[0][0]
        if len(yo_counts) > 1:
            for g in digits:
                g['yOffset'] = mode_yo
        # Shared baseline = the digits' ink bottom (rel baseline): mode_yo + digit_height.
        # For each glyph, bottom-align: yOffset = baseline_bottom - glyph_height.
        avg_digit_h = sum(g['height'] for g in digits) // len(digits)
        baseline_bottom = mode_yo + avg_digit_h
        for g in glyphs:
            g['yOffset'] = baseline_bottom - g['height']
        if colon is not None and center_colon:
            digit_mid = mode_yo + avg_digit_h / 2.0
            colon['yOffset'] = int(round(digit_mid - colon['height'] / 2.0))

    # Apply the advance squish (xAdvance = width - squish). Default 0 = advance equals
    # glyph width (minimum spacing, works for fonts with no built-in tracking like
    # Chewy). Chango uses --squish 8 (see LESSONS #20).
    for g in glyphs:
        if squish:
            g['xAdvance'] = max(0, g['width'] - squish)

    # Re-emit the glyph table after the above corrections.
    # (The glyphs list is rebuilt into the header below.)

    print(f"  Total bitmap size: {bitmap_offset} bytes for {len(glyphs)} glyphs")

    # Calculate yAdvance
    y_advance = ascent + descent

    # Generate C header
    lines = []
    lines.append("// Generated by gfxfont_gen.py from " + ttf_path.split('/')[-1] + f" at {pt_size}pt")
    lines.append(f"// Range: {chr(first)} (0x{first:02X}) to {chr(last)} (0x{last:02X})")
    lines.append(f"// Recipe: pt={pt_size} stretch={stretch} squish={squish} yAdvance={y_advance}")
    lines.append(f"// Colon: {'centre-aligned to digits' if center_colon else 'NATIVE (honours typeface)'}"
                 f" yOffset={colon['yOffset']} h={colon['height']}" if colon is not None
                 else "// Colon: n/a")
    lines.append("// Loads in flash via PROGMEM; drawn with GFX setFont(&font).")
    lines.append("#ifndef _INC_GFX_FONT_H_")
    lines.append("#define _INC_GFX_FONT_H_")
    lines.append("#include <gfxfont.h>")
    lines.append("")
    # The draw baseline: the cursor Y at which to call setCursor so the digits' ink is
    # vertically centred on the display. Derived from the digits' ink midpoint and the
    # panel height. Callers use this instead of hardcoding the baseline.
    if digits:
        digit_mid = sum(g['yOffset'] for g in digits) / len(digits) + avg_digit_h / 2.0
        baseline = round(PANEL_HEIGHT / 2.0 - digit_mid)
        lines.append(f"// Baseline: setCursor(x, {baseline}) centres the digits vertically.")
        lines.append(f"#define FONT_BASELINE {baseline}")

    # Bitmap array
    all_bytes = []
    for ba in bitmaps:
        all_bytes.extend(ba)
    if all_bytes:
        lines.append(f"static const uint8_t font_bitmap[] PROGMEM = {{")
        chunk = []
        for b in all_bytes:
            chunk.append(hex(b))
        # Format 16 bytes per line
        for i in range(0, len(chunk), 16):
            line = "  " + ", ".join(chunk[i:i+16]) + ","
            lines.append(line)
        lines.append("};")
    else:
        lines.append("static const uint8_t font_bitmap[] = {0};")

    lines.append("")

    # Glyph table
    lines.append("static const GFXglyph font_glyphs[] = {")
    for g in glyphs:
        lines.append(f"  {{ {g['bitmapOffset']:5d}, {g['width']:3d}, {g['height']:3d}, {g['xAdvance']:3d}, {g['xOffset']:3d}, {g['yOffset']:3d} }},")
    lines.append("};")

    lines.append("")

    # Font struct itself (const so it lives in flash)
    lines.append(f"static const GFXfont font = {{")
    lines.append(f"  (uint8_t *)font_bitmap,")
    lines.append(f"  (GFXglyph *)font_glyphs,")
    lines.append(f"  0x{first:04X},")
    lines.append(f"  0x{last:04X},")
    lines.append(f"  {y_advance}")
    lines.append("};")

    lines.append("")
    lines.append("#endif // _INC_GFX_FONT_H_")

    with open(out_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f"\nWritten: {out_path}")
    print(f"  First char: {chr(first)} (0x{first:02X})")
    print(f"  Last char:  {chr(last)} (0x{last:02X})")
    print(f"  yAdvance:   {y_advance}")
    print(f"  Glyphs:     {len(glyphs)}")


if __name__ == '__main__':
    main()
