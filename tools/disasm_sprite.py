#!/usr/bin/env python3
"""
disasm_sprite.py — walk the DolDoc tail of a .HC file as a stream of
packed CSprite records and print each opcode with its decoded fields.

Struct layouts come from Adam/Gr/Gr.HH in the TempleOS source. HolyC
appears to pack these structs (U8 type + I32 fields, no padding); we
verify empirically as we walk. Where a length is unknown or reserved
we log 'skip N' and jump forward by our best guess so we can continue.

Usage:
    py disasm_sprite.py <file.HC>
"""

import sys
import struct

SPT_NAMES = {
    0:  "END",              1:  "COLOR",           2:  "DITHER_COLOR",
    3:  "THICK",            4:  "PLANAR_SYMMETRY", 5:  "TRANSFORM_ON",
    6:  "TRANSFORM_OFF",    7:  "SHIFT",           8:  "PT",
    9:  "POLYPT",           10: "LINE",            11: "POLYLINE",
    12: "RECT",             13: "ROTATED_RECT",    14: "CIRCLE",
    15: "ELLIPSE",          16: "POLYGON",         17: "BSPLINE2",
    18: "BSPLINE2_CLOSED",  19: "BSPLINE3",        20: "BSPLINE3_CLOSED",
    21: "FLOOD_FILL",       22: "FLOOD_FILL_NOT",  23: "BITMAP",
    24: "MESH",             25: "SHIFTABLE_MESH",  26: "ARROW",
    27: "TEXT",             28: "TEXT_BOX",        29: "TEXT_DIAMOND",
}

def i32(buf, off):
    return struct.unpack_from('<i', buf, off)[0]
def u8(buf, off):
    return buf[off]

def disasm_op(buf, off):
    """
    Decode one op at `off`. Returns (op_name, human_string, new_off) or
    (None, error_msg, off+1) if we can't parse.
    """
    if off >= len(buf):
        return None, "eof", off
    t = buf[off]
    if t not in SPT_NAMES:
        return None, f"?? 0x{t:02x}", off + 1

    name = SPT_NAMES[t]
    # Fixed-size ops (packed layouts derived from CSprite*.HH classes):
    if t == 0:                                       # END
        return name, "", off + 1
    if t == 1:                                       # COLOR
        return name, f"color={buf[off+1]}", off + 2
    if t == 2:                                       # DITHER_COLOR
        v = struct.unpack_from('<H', buf, off+1)[0]
        return name, f"dither=0x{v:04x}", off + 3
    if t == 3:                                       # THICK
        return name, f"thick={i32(buf, off+1)}", off + 5
    if t in (5, 6):                                  # TRANSFORM_ON/OFF
        return name, "", off + 1
    if t in (7, 8, 21, 22):                          # SHIFT/PT/FLOOD_FILL*
        return name, f"({i32(buf, off+1)},{i32(buf, off+5)})", off + 9
    if t == 10:                                      # LINE (CSpritePtPt)
        x1, y1, x2, y2 = (i32(buf, off+1), i32(buf, off+5),
                          i32(buf, off+9), i32(buf, off+13))
        return name, f"({x1},{y1})-({x2},{y2})", off + 17
    if t == 12:                                      # RECT
        x, y, w, h = (i32(buf, off+1), i32(buf, off+5),
                      i32(buf, off+9), i32(buf, off+13))
        return name, f"({x},{y}) {w}x{h}", off + 17
    if t == 14:                                      # CIRCLE (CSpritePtRad)
        x, y, r = i32(buf, off+1), i32(buf, off+5), i32(buf, off+9)
        return name, f"({x},{y}) r={r}", off + 13
    if t == 16:                                      # POLYGON (PtWHAngSides)
        x, y, w, h = (i32(buf, off+1), i32(buf, off+5),
                      i32(buf, off+9), i32(buf, off+13))
        ang = struct.unpack_from('<d', buf, off+17)[0]
        sides = i32(buf, off+25)
        return name, f"({x},{y}) {w}x{h} ang={ang:.2f} sides={sides}", off + 29
    if t == 26:                                      # ARROW (PtPt)
        x1, y1, x2, y2 = (i32(buf, off+1), i32(buf, off+5),
                          i32(buf, off+9), i32(buf, off+13))
        return name, f"({x1},{y1})-({x2},{y2})", off + 17
    if t == 23:                                      # BITMAP (PtWH + u8[])
        x, y, w, h = (i32(buf, off+1), i32(buf, off+5),
                      i32(buf, off+9), i32(buf, off+13))
        # Naive: assume w*h pixel bytes follow. Truncate to buf size.
        pix = w * h
        return name, f"({x},{y}) {w}x{h} +{pix} pix bytes", off + 17 + pix

    # Variable-length ones we can't confidently step over yet.
    if t == 9:                                       # POLYPT (num + x + y + u8[num*2])
        num = i32(buf, off+1)
        x, y = i32(buf, off+5), i32(buf, off+9)
        return name, f"num={num} ({x},{y}) +{num*2} bytes", off + 13 + num * 2
    if t == 11:                                      # POLYLINE (num + u8[num*4])
        num = i32(buf, off+1)
        return name, f"num={num} +{num*4} bytes", off + 5 + num * 4
    if t == 24:                                      # MESH
        vcnt = i32(buf, off+1)
        tcnt = i32(buf, off+5)
        # vertices are usually (i8 x, i8 y, i8 z), triangles (u8 v0, v1, v2, color)
        pay = vcnt * 3 + tcnt * 4
        return name, f"v={vcnt} t={tcnt} +{pay} bytes", off + 9 + pay
    if t == 25:                                      # SHIFTABLE_MESH
        x, y, z = i32(buf, off+1), i32(buf, off+5), i32(buf, off+9)
        vcnt = i32(buf, off+13)
        tcnt = i32(buf, off+17)
        pay = vcnt * 3 + tcnt * 4
        return name, f"({x},{y},{z}) v={vcnt} t={tcnt} +{pay} bytes", off + 21 + pay
    if t in (17, 18, 19, 20):                        # BSPLINE variants
        num = i32(buf, off+1)
        return name, f"num={num} (variable, guessing)", off + 5 + num * 4

    # Fall-through for unknown fixed sizes we don't handle yet.
    return name, "?", off + 1

def find_sprite_starts(tail):
    """
    Sprite records inside the DolDoc tail begin after some CDocBin
    envelope bytes. Empirically each starts with 7+ zero bytes followed
    by a nonzero flag byte, then a couple of small u32 fields, then the
    first op. We find each such boundary and yield the payload offset
    (first plausible opcode).
    """
    starts = []
    i = 0
    while i < len(tail):
        if tail[i] != 0:
            i += 1
            continue
        j = i
        while j < len(tail) and tail[j] == 0:
            j += 1
        run = j - i
        if run >= 7:
            # After the zeros: flag byte + 8 bytes of "num,size" -> op stream
            # Payload starts ~15 bytes after the beginning of the zero run.
            payload = i + run + 8
            if 0 <= payload < len(tail):
                starts.append((i, payload))
        i = j
    return starts

def main(path):
    with open(path, 'rb') as f:
        data = f.read()
    nul = data.find(b'\x00')
    tail = data[nul + 1:]
    print(f"{path}: tail {len(tail)} bytes")

    starts = find_sprite_starts(tail)
    print(f"found {len(starts)} candidate sprite boundaries")

    seen_starts = set()
    for boundary, payload in starts:
        if payload in seen_starts:
            continue
        seen_starts.add(payload)
        print(f"\n=== sprite at offset 0x{boundary:04x} (payload 0x{payload:04x}) ===")
        off = payload
        # Walk ops up to 50 or until END or until we bail.
        for step in range(50):
            name, info, new_off = disasm_op(tail, off)
            if name is None:
                print(f"    0x{off:04x}: [{info}] -- stopping this sprite")
                break
            print(f"    0x{off:04x}: SPT_{name}  {info}")
            if name == "END" or new_off <= off:
                break
            off = new_off

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit("usage: disasm_sprite.py <file.HC>")
    main(sys.argv[1])
