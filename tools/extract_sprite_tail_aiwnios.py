#!/usr/bin/env python3
"""
extract_sprite_tail_aiwnios.py -- like extract_sprite_tail.py, but for
DolDoc dumps from the canewsin/templeos-1 aiwnios port. Those files use
the aiwnios CSprite layout, which packs SPT_BITMAP fields in the order
(type, width, height, x, y, pixels...) rather than Terry's original
(type, x, y, width, height, pixels...) that the shim (templeshim.c)
expects.

This script parses the CDocBin tail the same way as extract_sprite_tail
does, then swaps the two 8-byte pairs immediately after the opcode byte
for every SPT_BITMAP payload it emits. That yields headers whose SPT_BITMAP
bytes are Terry-canonical -- meaning Sprite3()/templeshim can render them
without any decoder changes.

Row stride (width_internal = (w + 7) & ~7) is identical between the two
formats, so no pixel-data reshuffling is needed.
"""

import argparse
import os
import struct
import sys

# Reuse the base extractor's helpers.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_sprite_tail import (
    HEADER_STRUCT, HEADER_SIZE, parse_tail, looks_like_sprite,
    find_text_bi_numbers, write_hex_dump, emit_header,
)

SPT_BITMAP = 0x17


def normalize_aiwnios_bitmap(data: bytes) -> bytes:
    """If the payload starts with SPT_BITMAP, swap the (w, h) and (x, y)
    header pairs to match Terry's canonical (x, y, w, h) layout that
    templeshim expects. Otherwise return data untouched."""
    if len(data) < 17 or data[0] != SPT_BITMAP:
        return data
    op = data[0:1]
    wh = data[1:9]     # aiwnios: width, height (i32 pair)
    xy = data[9:17]    # aiwnios: x, y (i32 pair)
    tail = data[17:]
    # After swap: op | x, y (as terry x1, y1) | w, h (as terry width, height)
    return op + xy + wh + tail


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('hc_path')
    ap.add_argument('--out-dir', default=None)
    ap.add_argument('--base', default=None)
    args = ap.parse_args()

    with open(args.hc_path, 'rb') as f:
        raw = f.read()
    nul = raw.find(b'\x00')
    if nul < 0:
        raise SystemExit(f"{args.hc_path}: no NUL terminator; not a DolDoc file")
    text_region = raw[:nul]
    tail = raw[nul + 1:]

    entries = list(parse_tail(tail))
    text_bi = find_text_bi_numbers(text_region)

    sprites = []
    for e in entries:
        if not looks_like_sprite(e['data']):
            continue
        e = dict(e)
        e['data'] = normalize_aiwnios_bitmap(e['data'])
        # Rewrite size so the emitted header matches the (possibly
        # swapped, same-length) blob.
        e['size'] = len(e['data'])
        sprites.append(e)

    base = args.base or os.path.splitext(os.path.basename(args.hc_path))[0].lower()
    header = emit_header(sprites, base, args.hc_path, text_bi)

    sys.stderr.write(f"{args.hc_path}: nul@{nul} tail={len(tail)}B\n")
    sys.stderr.write(f"  {len(entries)} CDocBin entr(y/ies), {len(sprites)} sprite(s)\n")
    for sp in sorted(sprites, key=lambda s: s['num']):
        first32 = sp['data'][:32].hex(' ')
        note = ""
        if sp['data'][0] == SPT_BITMAP:
            x, y, w, h = struct.unpack_from('<iiii', sp['data'], 1)
            note = f"  BITMAP({w}x{h} @ {x},{y})"
        sys.stderr.write(f"  BI={sp['num']:<3}size={sp['size']:>6}B{note}\n")
        sys.stderr.write(f"           first32=[{first32}]\n")

    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)
        header_path = os.path.join(args.out_dir, f"sprite_{base}.h")
        with open(header_path, 'w') as f:
            f.write(header)
        for sp in sprites:
            hex_path = os.path.join(args.out_dir, f"sprite_{base}_bi{sp['num']}.hex.txt")
            write_hex_dump(sp, hex_path, args.hc_path)
        sys.stderr.write(f"  wrote {header_path}\n")
    else:
        sys.stdout.write(header)


if __name__ == '__main__':
    main()
