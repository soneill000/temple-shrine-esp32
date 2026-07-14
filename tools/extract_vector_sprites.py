#!/usr/bin/env python3
"""
extract_vector_sprites.py — pull vector CSprite op streams out of a
.HC file and emit them as raw byte arrays for the C-side renderer to
walk. Also emits sprite 1 (bitmap) as a separate array plus its
declared origin/dimensions.

Depends on disasm_sprite: uses the same op-stream walker to find where
each sprite starts and ends.

Usage:
    py extract_vector_sprites.py Foo.HC > foo_vector_sprites.h
"""

import sys
from disasm_sprite import disasm_op, find_sprite_starts, SPT_NAMES

def find_sprite_regions(tail):
    """
    Walk each candidate sprite payload and record the byte range up to
    SPT_END. Returns list of (payload_start, payload_end_inclusive_of_END).
    """
    regions = []
    seen = set()
    for boundary, payload in find_sprite_starts(tail):
        if payload in seen:
            continue
        seen.add(payload)
        off = payload
        for _ in range(200):
            name, _, new_off = disasm_op(tail, off)
            if name is None:
                break
            if name == "END":
                regions.append((payload, new_off))
                break
            if new_off <= off:
                break
            off = new_off
    # Deduplicate overlapping regions (find_sprite_starts finds a lot of
    # false positives inside vector streams because zero bytes are common).
    regions.sort(key=lambda r: r[0])
    dedup = []
    for start, end in regions:
        if dedup and start <= dedup[-1][1]:
            continue
        dedup.append((start, end))
    return dedup

def emit_c_bytes(name, data):
    """C array of bytes with 12 per line."""
    print(f"static const uint8_t {name}[{len(data)}] = {{")
    for i in range(0, len(data), 12):
        row = data[i:i+12]
        print("    " + ", ".join(f"0x{b:02x}" for b in row) + ",")
    print("};")

def main(path):
    with open(path, 'rb') as f:
        data = f.read()
    nul = data.find(b'\x00')
    tail = data[nul + 1:]
    regions = find_sprite_regions(tail)
    base = path.split('/')[-1].split('\\')[-1].split('.')[0].lower()

    print(f"// Extracted from {path}")
    print(f"// {len(regions)} sprite record(s) in the DolDoc tail")
    print("#pragma once")
    print("#include <stdint.h>")
    print()

    for i, (start, end) in enumerate(regions, 1):
        payload = tail[start:end]
        # First byte tells us if it's a bitmap sprite (SPT_BITMAP = 23)
        if payload and payload[0] == 23:
            print(f"// sprite_{base}_{i}: SPT_BITMAP")
        else:
            first_op = SPT_NAMES.get(payload[0], f"0x{payload[0]:02x}") if payload else "empty"
            print(f"// sprite_{base}_{i}: vector op stream (first op: SPT_{first_op})")
        emit_c_bytes(f"sprite_{base}_{i}", payload)
        print()

    print(f"#define SPRITE_{base.upper()}_COUNT {len(regions)}")
    print(f"static const uint8_t *const SPRITE_{base.upper()}_TABLE[] = {{")
    for i in range(1, len(regions) + 1):
        print(f"    sprite_{base}_{i},")
    print("};")
    print(f"static const uint16_t SPRITE_{base.upper()}_SIZES[] = {{")
    for start, end in regions:
        print(f"    {end - start},")
    print("};")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit("usage: extract_vector_sprites.py <file.HC>")
    main(sys.argv[1])
