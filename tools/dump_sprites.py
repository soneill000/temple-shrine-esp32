#!/usr/bin/env python3
"""
dump_sprites.py — diagnostic BMP dumper for TempleOS .HC sprite payloads.
For each candidate sprite region in the .HC binary tail, emit BMPs at
several plausible (width, height) pairs so we can visually pick the one
that reads as a real image.

Colors follow the standard VGA 16-color palette. 0xFF pixels (Terry's
"transparent") are rendered magenta so they pop against everything else.

Usage:
    py dump_sprites.py <file.HC> [out_dir]
"""

import sys
import os
import struct

VGA_PALETTE = [
    (0x00, 0x00, 0x00),  # 0  black
    (0x00, 0x00, 0xAA),  # 1  blue
    (0x00, 0xAA, 0x00),  # 2  green
    (0x00, 0xAA, 0xAA),  # 3  cyan
    (0xAA, 0x00, 0x00),  # 4  red
    (0xAA, 0x00, 0xAA),  # 5  magenta
    (0xAA, 0x55, 0x00),  # 6  brown
    (0xAA, 0xAA, 0xAA),  # 7  ltgray
    (0x55, 0x55, 0x55),  # 8  dkgray
    (0x55, 0x55, 0xFF),  # 9  ltblue
    (0x55, 0xFF, 0x55),  # 10 ltgreen
    (0x55, 0xFF, 0xFF),  # 11 ltcyan
    (0xFF, 0x55, 0x55),  # 12 ltred
    (0xFF, 0x55, 0xFF),  # 13 ltmagenta
    (0xFF, 0xFF, 0x55),  # 14 yellow
    (0xFF, 0xFF, 0xFF),  # 15 white
]
TRANSPARENT_RGB = (255, 0, 255)   # bright magenta so it stands out

def write_bmp(path, w, h, pixel_bytes):
    """Write a 24-bit BMP from palette-indexed pixel bytes."""
    row_size = ((w * 3) + 3) & ~3
    total_pix = row_size * h
    file_size = 14 + 40 + total_pix
    with open(path, 'wb') as f:
        f.write(b'BM')
        f.write(struct.pack('<I', file_size))
        f.write(b'\x00\x00\x00\x00')
        f.write(struct.pack('<I', 14 + 40))
        f.write(struct.pack('<I', 40))
        f.write(struct.pack('<i', w))
        f.write(struct.pack('<i', h))
        f.write(struct.pack('<H', 1))
        f.write(struct.pack('<H', 24))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', total_pix))
        f.write(struct.pack('<I', 2835))    # 72dpi
        f.write(struct.pack('<I', 2835))
        f.write(struct.pack('<I', 0))
        f.write(struct.pack('<I', 0))
        for row in reversed(range(h)):
            out = bytearray()
            for col in range(w):
                p = pixel_bytes[row * w + col]
                if p == 0xFF:
                    r, g, b = TRANSPARENT_RGB
                elif p < 16:
                    r, g, b = VGA_PALETTE[p]
                else:
                    r, g, b = (100, 100, 100)
                out.extend([b, g, r])
            while len(out) < row_size:
                out.append(0)
            f.write(bytes(out))

def find_candidate_regions(tail):
    """
    Locate spans in the tail that look like they hold bitmap pixel data.
    Heuristic: a run that's dominated by 0xFF (transparent) mixed with
    low palette indices. Returns list of (start, length) where the
    pixel data probably lives.
    """
    regions = []
    i = 0
    N = len(tail)
    while i < N:
        # Skip until we find a byte that looks like a pixel (0xFF or 0..15)
        if tail[i] != 0xFF and tail[i] >= 16:
            i += 1
            continue
        j = i
        while j < N and (tail[j] == 0xFF or tail[j] < 16):
            j += 1
        length = j - i
        if length >= 80:
            regions.append((i, length))
        i = j
    return regions

def dump_region(tail, start, length, out_dir, region_index):
    """Dump this region at a bunch of plausible dimensions."""
    # Try dimensions whose w*h fits within `length`, spanning common
    # TempleOS sprite sizes.
    tries = []
    for w in (8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64):
        for h in (8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64):
            if w * h <= length and w * h >= length - w:  # roughly matches
                tries.append((w, h))
    # Also always emit the "declared-square" and a bunch of specific favorites
    for wh in (16, 24, 32, 48):
        if wh * wh <= length:
            tries.append((wh, wh))
    seen = set()
    for w, h in tries:
        if (w, h) in seen:
            continue
        seen.add((w, h))
        pix = tail[start:start + w * h]
        path = os.path.join(out_dir, f"region{region_index:02d}_off{start:04x}_len{length}_{w:03d}x{h:03d}.bmp")
        write_bmp(path, w, h, pix)

def main(hc_path, out_dir):
    with open(hc_path, 'rb') as f:
        data = f.read()
    nul = data.find(b'\x00')
    if nul < 0:
        raise SystemExit("no NUL — not a DolDoc file")
    tail = data[nul + 1:]
    os.makedirs(out_dir, exist_ok=True)
    regions = find_candidate_regions(tail)
    print(f"{hc_path}: tail {len(tail)} bytes, {len(regions)} candidate region(s)")
    for i, (start, length) in enumerate(regions):
        print(f"  region {i:02d}: offset 0x{start:04x} length {length}")
        dump_region(tail, start, length, out_dir, i)
    print(f"BMPs written to {out_dir}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit("usage: dump_sprites.py <file.HC> [out_dir]")
    hc = sys.argv[1]
    outd = sys.argv[2] if len(sys.argv) > 2 else 'sprites_out'
    main(hc, outd)
