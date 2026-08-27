#!/usr/bin/env python3
"""
eClock Icon Tool — Extract glyphs from an icon font as 1-bit C bitmap arrays.

Usage:
    # Export a single icon (auto-trimmed to content)
    python icon_tool.py export "C:/path/to/icons.ttf" 14 0xE86C

    # Export with no trimming (preserves raw glyph bounding box)
    python icon_tool.py export "C:/path/to/icons.ttf" 14 0xE86C --no-trim

    # Batch export named icons (auto-trimmed)
    python icon_tool.py batch "C:/path/to/icons.ttf" 14 \
        --icons "check=0xE86C,error=0xE000,refresh=0xE5D5" \
        --header "path/to/material_icons.h" \
        --guard "ECLOCK_MATERIAL_ICONS_H"

    # Batch export with no trimming
    python icon_tool.py batch "C:/path/to/icons.ttf" 14 \
        --icons "check=0xE86C,error=0xE000,refresh=0xE5D5" --no-trim

    # Render a sprite sheet of multiple icons for visual review
    python icon_tool.py sprite "C:/path/to/icons.ttf" 17 0xE86C 0xE000 0xE5D5
    python icon_tool.py sprite "C:/path/to/icons.ttf" 17 \
        --icons "synced=0xE86C,failed=0xE000" --output preview.png

The tool automatically:
- Renders the glyph at the requested size
- Trims empty rows/columns by default (use --no-trim to preserve raw size)
- Packs to MSB-first byte arrays in PROGMEM format

Depends on Pillow: pip install Pillow
"""

import sys
import os
import textwrap

try:
    from PIL import ImageFont, Image, ImageDraw
except ImportError:
    print("ERROR: Pillow is required. Run: pip install Pillow")
    sys.exit(1)


def export_glyph(font_path, pt_size, codepoint, no_trim=False):
    """Render a glyph and return the row-trimmed bitmap data + size."""

    font = ImageFont.truetype(font_path, pt_size)
    char = chr(codepoint)
    bbox = font.getbbox(char)

    if not bbox or (bbox[2] == 0 and bbox[3] == 0):
        print(f"Warning: glyph U+{codepoint:04X} has no bounding box")
        return None, 0, 0

    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]

    # Render to 1-bit image
    img = Image.new('1', (w, h), 1)
    draw = ImageDraw.Draw(img)
    draw.text((-bbox[0], -bbox[1]), char, font=font, fill=0)

    pixels = img.load()

    # When no_trim is True, skip all trimming and pack the raw glyph
    if no_trim:
        bytes_out = []
        for y in range(h):
            byte = 0
            bits = 0
            for x in range(w):
                if pixels[x, y] == 0:
                    byte |= (1 << (7 - bits))
                bits += 1
                if bits == 8:
                    bytes_out.append(byte)
                    byte = 0
                    bits = 0
            if bits > 0:
                bytes_out.append(byte)
        return bytes_out, w, h

    # Trim empty rows from top and bottom
    pixels = img.load()
    top = 0
    while top < h:
        if any(pixels[x, top] == 0 for x in range(w)):
            break
        top += 1

    bottom = h - 1
    while bottom >= top:
        if any(pixels[x, bottom] == 0 for x in range(w)):
            break
        bottom -= 1

    trim_top = top
    trimmed_h = bottom - top + 1

    if trimmed_h <= 0:
        return None, w, 0

    # Pack rows to MSB-first byte array
    bytes_out = []
    for y in range(trim_top, top + trimmed_h):
        byte = 0
        bits = 0
        for x in range(w):
            if pixels[x, y] == 0:
                byte |= (1 << (7 - bits))
            bits += 1
            if bits == 8:
                bytes_out.append(byte)
                byte = 0
                bits = 0
        if bits > 0:
            bytes_out.append(byte)

    # Trim to actual content width (remove empty columns)
    col_left = 0
    while col_left < w:
        if any(pixels[x, y] == 0 for y in range(top, top + trimmed_h) for x in [col_left]):
            break
        col_left += 1
    col_right = w - 1
    while col_right >= col_left:
        if any(pixels[x, y] == 0 for y in range(top, top + trimmed_h) for x in [col_right]):
            break
        col_right -= 1

    trimmed_w = col_right - col_left + 1

    # Re-pack with column trim
    if trimmed_w < w:
        trimmed_bytes = []
        for y in range(trim_top, top + trimmed_h):
            byte = 0
            bits = 0
            for x in range(col_left, col_right + 1):
                if pixels[x, y] == 0:
                    byte |= (1 << (7 - bits))
                bits += 1
                if bits == 8:
                    trimmed_bytes.append(byte)
                    byte = 0
                    bits = 0
            if bits > 0:
                trimmed_bytes.append(byte)
        w = trimmed_w
        bytes_out = trimmed_bytes

    return bytes_out, w, trimmed_h


def format_c_array(name, bytes_data, w, h):
    """Format bitmap bytes as a C PROGMEM array."""
    lines = []
    lines.append(f"// {name} icon: {w}x{h}px")
    lines.append(f"static const uint8_t icon_{name}_bitmap[] PROGMEM = {{")
    for i in range(0, len(bytes_data), 12):
        chunk = bytes_data[i:i+12]
        line = '  ' + ', '.join(f'0x{b:02X}' for b in chunk) + ','
        lines.append(line)
    lines.append("};")
    lines.append(f"static const uint8_t icon_{name}_w = {w};")
    lines.append(f"static const uint8_t icon_{name}_h = {h};")
    return '\n'.join(lines)


def cmd_sprite(ttf_path, pt_size, icons_dict, output_path=None):
    """Render icons as a sprite sheet PNG for visual review."""
    font = ImageFont.truetype(ttf_path, pt_size)

    if not output_path:
        # Default to project root (firmware/tools/ → firmware/ → project_root)
        script_dir = os.path.dirname(os.path.abspath(__file__))    # firmware/tools/
        project_root = os.path.dirname(os.path.dirname(script_dir))  # project root
        font_name = os.path.splitext(os.path.basename(ttf_path))[0]
        output_path = os.path.join(project_root, f'sprite_{font_name}_{pt_size}pt.png')

    # Render each icon to get dimensions
    rendered = []
    max_label_w = 0
    for name, cp in icons_dict.items():
        bbox = font.getbbox(chr(cp))
        if not bbox or bbox[2] == 0:
            print(f"WARNING: '{name}' has no glyph at U+{cp:04X}")
            continue
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        label = f'U+{cp:04X}  {name}  [{w}x{h}]'
        rendered.append((name, cp, w, h, label))
        max_label_w = max(max_label_w, len(label))

    if not rendered:
        print("ERROR: no icons rendered")
        sys.exit(1)

    # Auto-size based on actual glyph dimensions
    max_ink_w = max(r[2] for r in rendered)
    max_ink_h = max(r[3] for r in rendered)
    cell_pad = 4
    cell_h = max_ink_h + cell_pad * 2 + 4  # +4 for border + clearance
    label_w = max_label_w * 7 + 20
    icon_area_w = max_ink_w + cell_pad * 2 + 4  # +4 for border
    cell_w = label_w + icon_area_w
    sheet_h = cell_h * len(rendered)

    sheet = Image.new('RGB', (cell_w, sheet_h), (255, 255, 255))
    draw = ImageDraw.Draw(sheet)

    for i, (name, cp, w, h, label) in enumerate(rendered):
        y = i * cell_h
        # Render icon (black on white = ePaper colour scheme)
        icon_img = Image.new('1', (w, h), 1)  # white bg
        idd = ImageDraw.Draw(icon_img)
        idd.text((-font.getbbox(chr(cp))[0], -font.getbbox(chr(cp))[1]),
                 chr(cp), font=font, fill=0)  # black fg
        # Convert to 'L' and invert so ink=255 (opaque), bg=0 (transparent)
        mask = Image.eval(icon_img.convert('L'), lambda x: 255 - x)
        icon_rgb = Image.new('RGB', (w, h), (255, 255, 255))  # white bg
        icon_rgb.paste((0, 0, 0), mask=mask)  # black fg

        # Centre glyph horizontally within the widest glyph's space,
        # and vertically within the cell
        x_offset = cell_pad + 2 + (max_ink_w - w) // 2
        y_offset = (cell_h - h) // 2
        sheet.paste(icon_rgb, (x_offset, y + y_offset))

        # Black border around the icon cell (sized for the widest glyph)
        border_x = cell_pad + 1
        border_y = y + y_offset - 1
        border_w = max_ink_w + 2
        border_h = h + 2
        draw.rectangle([border_x, border_y, border_x + border_w, border_y + border_h],
                       outline=(0, 0, 0))

        draw.text((icon_area_w + 4, y + 3), label, fill=0)

    sheet.save(output_path)
    print(f"Sprite sheet saved: {output_path}")
    print(f"  Icons: {len(rendered)}")
    print(f"  Size: {cell_w}x{sheet_h}px")


def cmd_export(ttf_path, pt_size, codepoint_hex):
    """Export a single glyph and print the C array."""
    if not os.path.exists(ttf_path):
        print(f"ERROR: file not found: {ttf_path}")
        sys.exit(1)

    codepoint = int(codepoint_hex, 16) if isinstance(codepoint_hex, str) and codepoint_hex.startswith('0x') else int(codepoint_hex)

    # Get name from metadata if possible, else use hex
    name = codepoint_hex

    result, w, h = export_glyph(ttf_path, pt_size, codepoint)
    if result is None or h == 0:
        print(f"ERROR: could not render glyph {codepoint_hex}")
        sys.exit(1)

    print(f"// Extracted from {os.path.basename(ttf_path)} at {pt_size}pt")
    print(f"// Codepoint: U+{codepoint:04X}")
    print()
    print(format_c_array(name, result, w, h))


def cmd_batch(ttf_path, pt_size, icons_str, header_path=None, guard=None, no_trim=False):
    """Batch export multiple icons and write to a header file."""
    if not os.path.exists(ttf_path):
        print(f"ERROR: file not found: {ttf_path}")
        sys.exit(1)

    # Parse icons string: "check=0xE86C,error=0xE000,refresh=0xE5D5"
    icons = {}
    for pair in icons_str.split(','):
        pair = pair.strip()
        if '=' not in pair:
            print(f"ERROR: invalid icon spec '{pair}', expected 'name=0xCODE'")
            sys.exit(1)
        name, code = pair.split('=', 1)
        icons[name.strip()] = int(code.strip(), 16)

    # Build header file content
    lines = []
    lines.append("/*")
    lines.append(f" * Generated by icon_tool.py from {os.path.basename(ttf_path)} at {pt_size}pt")
    lines.append(" *")
    lines.append(" * Icons extracted:")
    for name, cp in icons.items():
        lines.append(f" *   {name}: U+{cp:04X}")
    lines.append(" */")
    lines.append("")

    if guard:
        lines.append(f"#ifndef {guard}")
        lines.append(f"#define {guard}")
        lines.append("")

    for name, cp in icons.items():
        result, w, h = export_glyph(ttf_path, pt_size, cp, no_trim)
        if result is None or h == 0:
            print(f"WARNING: could not render '{name}' (U+{cp:04X}), skipping")
            continue
        lines.append(format_c_array(name, result, w, h))
        lines.append("")

    if guard:
        lines.append(f"#endif // {guard}")

    output = '\n'.join(lines)

    if header_path:
        with open(header_path, 'w') as f:
            f.write(output)
        print(f"Written: {header_path}")
    else:
        print(output)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "export":
        if len(sys.argv) < 5:
            print("Usage: python icon_tool.py export <ttf_path> <pt_size> <codepoint>")
            sys.exit(1)
        cmd_export(sys.argv[2], int(sys.argv[3]), sys.argv[4])

    elif cmd == "batch":
        if len(sys.argv) < 4:
            print("Usage: python icon_tool.py batch <ttf_path> <pt_size> --icons ... [--header path] [--guard NAME]")
            sys.exit(1)
        ttf = sys.argv[2]
        pt = int(sys.argv[3])

        icons_str = None
        header_path = None
        guard = None
        no_trim = False

        i = 4
        while i < len(sys.argv):
            if sys.argv[i] == "--icons" and i + 1 < len(sys.argv):
                icons_str = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == "--header" and i + 1 < len(sys.argv):
                header_path = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == "--guard" and i + 1 < len(sys.argv):
                guard = sys.argv[i + 1]
                i += 2
            elif sys.argv[i] == "--no-trim":
                no_trim = True
                i += 1
            else:
                i += 1

        if not icons_str:
            print("ERROR: --icons is required for batch command")
            sys.exit(1)

        cmd_batch(ttf, pt, icons_str, header_path, guard, no_trim)

    elif cmd == "sprite":
        if len(sys.argv) < 5:
            print("Usage: python icon_tool.py sprite <ttf_path> <pt_size> <cp1> [cp2 ...] [--icons ...] [--output path]")
            sys.exit(1)
        ttf = sys.argv[2]
        pt = int(sys.argv[3])
        cps = []
        icons_dict = {}
        output_path = None
        i = 4
        while i < len(sys.argv):
            if sys.argv[i] == "--icons" and i + 1 < len(sys.argv):
                # Parse name=cp pairs
                for pair in sys.argv[i + 1].split(','):
                    n, c = pair.split('=')
                    icons_dict[n.strip()] = int(c.strip(), 16)
                i += 2
            elif sys.argv[i] == "--output" and i + 1 < len(sys.argv):
                output_path = sys.argv[i + 1]
                i += 2
            elif sys.argv[i].startswith('0x'):
                cps.append(int(sys.argv[i], 16))
                i += 1
            else:
                i += 1
        # If no named icons, use codepoints directly
        if not icons_dict:
            for cp in cps:
                icons_dict[f'U+{cp:04X}'] = cp
        cmd_sprite(ttf, pt, icons_dict, output_path)

    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
