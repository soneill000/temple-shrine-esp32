#!/usr/bin/env python3
"""
extract_sprites.py — pull bitmap sprites out of a TempleOS .HC file and
emit them as C arrays of TempleOS palette indices.

A .HC file is a DolDoc document: text region, first NUL, then a series
of appended CDocBin records. Each CDocBin holds a sprite payload.
Sprites come in two flavors: raw palette-indexed bitmaps (0xFF =
transparent) and vector op streams (SPT_COLOR, SPT_LINE, ...).

This script targets bitmap sprites only. Empirical strategy: scan the
tail for byte 0x17 (SPT_BITMAP opcode) preceded by ~8 zeros and followed
by an 8-byte i64 offset, then two u32s for width/height. Then use those
to slice out the pixel bytes.

Usage:
    py extract_sprites.py Foo.HC > foo_sprites.h
"""

import sys
import struct

def looks_like_bitmap_at(tail: bytes, idx: int):
    """Check if `idx` is the SPT_BITMAP opcode of a well-formed record.
    Returns (w, h, pixels_offset) or None."""
    if idx < 8 or idx + 16 >= len(tail):
        return None
    # Require ~4 zeros in the 8 bytes right before the opcode (these are
    # part of the enclosing CDocBin / CSpriteBase padding).
    prefix = tail[idx - 8:idx]
    if prefix.count(0) < 4:
        return None
    # Skip 8 bytes of i64 origin/offset.
    wh_off = idx + 1 + 8
    if wh_off + 8 > len(tail):
        return None
    w = struct.unpack_from('<I', tail, wh_off)[0]
    h = struct.unpack_from('<I', tail, wh_off + 4)[0]
    # Sanity clamp.
    if not (4 <= w <= 128 and 4 <= h <= 128):
        return None
    # The declared w,h at the header are Terry's *origin offsets*, not
    # the actual pixel-data dimensions in every case. Try common sprite
    # sizes first (24x24 is the FlapBat / small-game standard), fall
    # back to the declared values.
    for tw, th in [(24, 24), (32, 32), (16, 16), (w, h), (w + h, h), (w, h + w)]:
        pix_off = wh_off + 8
        sz = tw * th
        if pix_off + sz > len(tail):
            continue
        px = tail[pix_off:pix_off + sz]
        ff = sum(1 for b in px if b == 0xff)
        low = sum(1 for b in px if b < 16 or b == 0xff)
        if ff / sz > 0.2 and low / sz > 0.9:
            return (tw, th, pix_off)
    return None

def find_bitmap_sprites(tail: bytes):
    found = []
    seen_offsets = set()
    for i in range(len(tail)):
        if tail[i] != 0x17:
            continue
        result = looks_like_bitmap_at(tail, i)
        if result is None:
            continue
        w, h, pix_off = result
        if pix_off in seen_offsets:
            continue
        seen_offsets.add(pix_off)
        found.append((i, w, h, tail[pix_off:pix_off + w * h]))
    return found

def emit_c_array(name: str, w: int, h: int, px: bytes) -> str:
    lines = [f"// {name}: {w}x{h} bitmap, TempleOS palette indices.",
             f"// 0xFF = transparent. 0..15 = palette (see palette.h).",
             f"#define {name.upper()}_W {w}",
             f"#define {name.upper()}_H {h}",
             f"static const uint8_t {name.upper()}[{w}*{h}] = {{"]
    for row in range(h):
        row_bytes = px[row*w:(row+1)*w]
        hexs = ", ".join(f"0x{b:02x}" for b in row_bytes)
        lines.append(f"    {hexs},")
    lines.append("};")
    return "\n".join(lines)

def main(path: str):
    with open(path, 'rb') as f:
        data = f.read()
    nul = data.find(b'\x00')
    if nul < 0:
        raise SystemExit("no NUL terminator — not a DolDoc file?")
    tail = data[nul + 1:]
    sprites = find_bitmap_sprites(tail)
    if not sprites:
        raise SystemExit("no bitmap sprites found")

    base = path.split('/')[-1].split('\\')[-1].split('.')[0].lower()
    print(f"// Extracted from {path}")
    print(f"// {len(sprites)} bitmap sprite(s) found in the DolDoc tail")
    print("#pragma once")
    print("#include <stdint.h>")
    print()
    for i, (off, w, h, px) in enumerate(sprites, 1):
        name = f"sprite_{base}_{i}"
        print(emit_c_array(name, w, h, px))
        print()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit("usage: extract_sprites.py <file.HC>")
    main(sys.argv[1])
