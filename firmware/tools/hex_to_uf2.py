#!/usr/bin/env python3
"""Convert an Intel HEX (.hex) to a UF2 file for the nRF52840 BLE board.

This is a minimal, self-contained replacement for microsoft/uf2 uf2conv.py.
The release WF's uf2conv.py -c output placed the family ID in the fileSize slot
and omitted the magic-end fields, so the produced .uf2 did not flash. We write
the header explicitly (with a familyID slot and valid magic end), which the
Seeed XIAO bootloader accepts.

Usage: hex_to_uf2.py <in.hex> <out.uf2> --base 0x27000 --family 0xada52840
"""
import argparse
import struct

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END0 = 0x0AB16F30
UF2_MAGIC_END1 = 0x8117E2CE
UF2_FLAG_FAMILY_PRESENT = 0x00002000
BLOCK_PAYLOAD = 256


def parse_hex(path):
    """Return {address: byte} dict from an Intel HEX file."""
    data = {}
    base = 0
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            hexstr = line[1:]
            raw = bytes.fromhex(hexstr)
            nbytes = raw[0]
            addr = (raw[1] << 8) | raw[2]
            rectype = raw[3]
            payload = raw[4:4 + nbytes]
            if rectype == 0x00:            # data
                data[base + addr] = payload
            elif rectype == 0x01:          # EOF
                break
            elif rectype == 0x02:          # extended segment address
                base = ((payload[0] << 8) | payload[1]) << 4
            elif rectype == 0x04:          # extended linear address
                base = ((payload[0] << 8) | payload[1]) << 16
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('in_hex')
    ap.add_argument('out_uf2')
    ap.add_argument('--base', type=lambda x: int(x, 0), default=0x27000)
    ap.add_argument('--family', type=lambda x: int(x, 0), default=0xada52840)
    args = ap.parse_args()

    data = parse_hex(args.in_hex)

    # Build a packed bytearray at the base address.
    min_addr = min(data)
    max_end = max(a + len(b) for a, b in data.items())
    image_size = max_end - min_addr
    image = bytearray(image_size)
    for a, b in data.items():
        off = a - min_addr
        for i, byte in enumerate(b):
            image[off + i] = byte

    # Pad image to a whole block.
    if len(image) % BLOCK_PAYLOAD:
        image += bytes(BLOCK_PAYLOAD - (len(image) % BLOCK_PAYLOAD))

    num_blocks = len(image) // BLOCK_PAYLOAD
    family = args.family
    # Family-present bit only; the family ID lives in its own header slot below.
    flags = UF2_FLAG_FAMILY_PRESENT

    out = bytearray()
    for bno in range(num_blocks):
        payload = image[bno * BLOCK_PAYLOAD:(bno + 1) * BLOCK_PAYLOAD]
        taddr = args.base + bno * BLOCK_PAYLOAD
        header = struct.pack(
            '<IIIIIIIII',
            UF2_MAGIC_START0, UF2_MAGIC_START1, flags, taddr,
            len(payload), bno, num_blocks, image_size,
            family,  # <-- familyID in its own slot; real size above
        )
        # Now add the magic end (2 more uint32).
        header += struct.pack('<II', UF2_MAGIC_END0, UF2_MAGIC_END1)
        block = header + bytes(payload)
        block += bytes(512 - len(block))   # pad to 512
        out += block

    with open(args.out_uf2, 'wb') as f:
        f.write(out)
    print(f"wrote {args.out_uf2}: {len(out)} bytes, {num_blocks} blocks, "
          f"base {hex(args.base)}, family {hex(family)}")


if __name__ == '__main__':
    main()
