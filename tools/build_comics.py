#!/usr/bin/env python3
"""
build_comics.py -- turn the raw Moses comics fetched by fetch_comics.py into
one C header the badge firmware can include.

Each moses_NN.raw is:
   [DolDoc text bytes] NUL [CDocBin tail]

Tail parsing matches tools/extract_sprite_tail.py exactly:
   struct entry { u32 num; u32 flags; u32 size; u32 use_cnt; u8 data[size]; }
   (little-endian, tightly packed). data[0] is the first SPT_* opcode.

Some comics have no sprite tail (text-only) -- those get `sprite=NULL,
sprite_size=0` in the emitted table.

Terry's text is preserved VERBATIM. Bytes >= 0x80 are replaced with SPACE
(0x20) in the C literal so the badge's 7-bit 8x8 font renders cleanly.
Backslashes, double-quotes, and literal newlines are escaped.

USAGE
    py build_comics.py [--in-dir DIR] [--out FILE]
"""

import argparse
import glob
import os
import re
import struct
import sys


HEADER_STRUCT = struct.Struct("<IIII")  # num, flags, size, use_cnt
HEADER_SIZE = HEADER_STRUCT.size        # 16
VALID_SPT = set(range(0, 30))


def parse_tail(tail: bytes):
    """Yield CDocBin entries from a tail blob. Same format as
    tools/extract_sprite_tail.py -- see that file for the reference."""
    off = 0
    while off + HEADER_SIZE <= len(tail):
        num, flags, size, use_cnt = HEADER_STRUCT.unpack_from(tail, off)
        payload_off = off + HEADER_SIZE
        if payload_off + size > len(tail):
            # Truncated / garbage tail: stop cleanly rather than crash.
            sys.stderr.write(
                f"    warn: CDocBin at 0x{off:04x} claims size={size} but "
                f"only {len(tail) - payload_off} bytes remain; stopping\n"
            )
            return
        yield {
            "tail_off": off,
            "num": num,
            "flags": flags,
            "size": size,
            "use_cnt": use_cnt,
            "data": tail[payload_off:payload_off + size],
        }
        off += HEADER_SIZE + size


def sanitize_text_for_c(text_bytes: bytes) -> tuple[str, int]:
    """Return (c_string_literal_body, replacements_made).

    Bytes >= 0x80 -> space. Backslash, double-quote, and newlines escaped.
    Other control chars (< 0x20) except \\n, \\r, \\t left literal? -> keep
    \\n / \\r / \\t as escapes, drop bare NULs (there shouldn't be any),
    and let other low bytes pass through as \\xNN so nothing surprises the
    compiler."""
    out = []
    replaced_hi = 0
    for b in text_bytes:
        if b >= 0x80:
            out.append(" ")
            replaced_hi += 1
            continue
        if b == ord("\\"):
            out.append("\\\\")
        elif b == ord('"'):
            out.append('\\"')
        elif b == ord("?"):
            # Escape as \? so consecutive ??X sequences don't form ISO C
            # trigraphs (Terry uses `??!!` in moses07 which would otherwise
            # be munged to `|!` at compile time).
            out.append("\\?")
        elif b == ord("\n"):
            out.append("\\n")
        elif b == ord("\r"):
            out.append("\\r")
        elif b == ord("\t"):
            out.append("\\t")
        elif b == 0x00:
            # Shouldn't ever hit -- text region is by definition pre-NUL.
            out.append("\\0")
        elif b < 0x20:
            out.append(f"\\x{b:02x}")
        else:
            out.append(chr(b))
    return "".join(out), replaced_hi


def hex_array_body(data: bytes, per_row: int = 12) -> str:
    lines = []
    for i in range(0, len(data), per_row):
        chunk = data[i:i + per_row]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def process_one(raw_path: str):
    """Return dict describing one comic:
       {num, text_c, text_bytes_len, replaced_hi, sprite_bytes|None,
        entry_count, first_bytes_note}"""
    with open(raw_path, "rb") as f:
        data = f.read()
    m = re.search(r"moses_(\d+)\.raw$", os.path.basename(raw_path))
    if not m:
        raise SystemExit(f"can't parse comic number from {raw_path}")
    num = int(m.group(1))

    nul = data.find(b"\x00")
    if nul < 0:
        text_bytes = data
        tail = b""
    else:
        text_bytes = data[:nul]
        tail = data[nul + 1:]

    text_c, replaced_hi = sanitize_text_for_c(text_bytes)

    first_bytes_note = None
    if text_bytes and not (0x20 <= text_bytes[0] < 0x7F):
        first_bytes_note = text_bytes[:20]

    entries = list(parse_tail(tail))
    sprite_entries = [e for e in entries if e["data"] and e["data"][0] in VALID_SPT]
    sprite = sprite_entries[0]["data"] if sprite_entries else None

    return {
        "num": num,
        "text_c": text_c,
        "text_bytes_len": len(text_bytes),
        "replaced_hi": replaced_hi,
        "sprite": sprite,
        "sprite_first_op": sprite[0] if sprite else None,
        "entry_count": len(entries),
        "sprite_entry_count": len(sprite_entries),
        "first_bytes_note": first_bytes_note,
        "raw_path": raw_path,
        "raw_size": len(data),
    }


def emit_header(comics: list, header_path: str, in_dir: str) -> str:
    lines = []
    lines.append("// Auto-generated by tools/build_comics.py.")
    lines.append("// Do not edit -- regenerate from tools/comics_out/moses_*.raw")
    lines.append("// Source: canewsin/templeos-1 iso/apps/afteregypt/comics/*.txt.z")
    lines.append("//")
    lines.append("// Terry's DolDoc text is preserved verbatim (bytes >= 0x80 -> space).")
    lines.append("// The sprite bytes are raw SPT_* opcode streams -- feed straight to")
    lines.append("// Sprite3()/the badge sprite decoder.")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char    *name;         // e.g. \"MOSES 01\"")
    lines.append("    const char    *text;         // full DolDoc text (with $FG,N$, $WW,N$, $SP,...$ tags intact)")
    lines.append("    const uint8_t *sprite;       // NULL if no sprite tail")
    lines.append("    unsigned       sprite_size;  // 0 if no sprite")
    lines.append("} moses_comic_t;")
    lines.append("")

    # One array per comic that has a sprite tail.
    for c in comics:
        if c["sprite"] is None:
            continue
        name = f"COMIC_MOSES_{c['num']:02d}_SPR"
        lines.append(f"// moses{c['num']:02d}: {len(c['sprite'])} bytes, "
                     f"first_op=0x{c['sprite_first_op']:02x}, "
                     f"entries_in_tail={c['entry_count']}")
        lines.append(f"static const uint8_t {name}[{len(c['sprite'])}] = {{")
        lines.append(hex_array_body(c["sprite"]))
        lines.append("};")
        lines.append("")

    # The dispatch table.
    lines.append("static const moses_comic_t MOSES_COMICS[] = {")
    for c in comics:
        cname = f"\"MOSES {c['num']:02d}\""
        # Wrap long text literals across multiple lines using adjacent
        # string literal concatenation -- one line per Terry line so diffs
        # stay readable.
        text_lit = c["text_c"]
        pieces = text_lit.split("\\n")
        if len(pieces) > 1:
            # Rebuild as "line\n" "line\n" ... with a "" placeholder if empty.
            joined_lit = "\n            "
            joined_lit += "\n            ".join(
                f"\"{p}\\n\"" for p in pieces[:-1]
            )
            # The final piece has no trailing \n (that's the split's tail).
            if pieces[-1]:
                joined_lit += f"\n            \"{pieces[-1]}\""
        else:
            joined_lit = f"\"{text_lit}\""

        if c["sprite"] is None:
            spr_ref = "NULL, 0"
        else:
            aname = f"COMIC_MOSES_{c['num']:02d}_SPR"
            spr_ref = f"{aname}, sizeof({aname})"
        lines.append(f"    {{ {cname},")
        lines.append(f"      {joined_lit},")
        lines.append(f"      {spr_ref} }},")
    lines.append("};")
    lines.append(f"static const int MOSES_COMICS_N = {len(comics)};")
    lines.append("")

    out = "\n".join(lines)
    with open(header_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--in-dir", default=os.path.join(script_dir, "comics_out"),
                    help="dir of moses_NN.raw files (default: tools/comics_out)")
    ap.add_argument("--out", default=None,
                    help="output header (default: firmware/templeshrine/"
                         "src/sprite_comics.h relative to this script)")
    args = ap.parse_args()

    header_out = args.out or os.path.join(
        script_dir, "..", "src", "sprite_comics.h"
    )
    header_out = os.path.abspath(header_out)

    raws = sorted(glob.glob(os.path.join(args.in_dir, "moses_*.raw")))
    if not raws:
        raise SystemExit(f"no moses_*.raw files in {args.in_dir}; "
                         f"run fetch_comics.py first")

    comics = []
    total_replaced = 0
    for p in raws:
        c = process_one(p)
        comics.append(c)
        sys.stderr.write(
            f"  moses{c['num']:02d}: raw={c['raw_size']}B text={c['text_bytes_len']}B "
            f"entries={c['entry_count']} sprite="
            + (f"{len(c['sprite'])}B (op=0x{c['sprite_first_op']:02x})"
               if c['sprite'] else "NONE")
            + (f" nonascii={c['replaced_hi']}" if c['replaced_hi'] else "")
            + "\n"
        )
        if c["first_bytes_note"] is not None:
            sys.stderr.write(f"    note: text does not start printable; "
                             f"first20={c['first_bytes_note']!r}\n")
        total_replaced += c["replaced_hi"]

    header = emit_header(comics, header_out, args.in_dir)
    hdr_bytes = len(header.encode("utf-8"))
    sys.stderr.write(f"\nwrote {header_out}\n")
    sys.stderr.write(f"header size: {hdr_bytes} bytes ({hdr_bytes/1024:.2f} KB)\n")
    sys.stderr.write(f"total non-ASCII text bytes replaced with space: "
                     f"{total_replaced}\n")

    # ------- Manual eyeball preview: first comic text + sprite header. -------
    first = comics[0]
    sys.stderr.write("\n---- PREVIEW: moses_{:02d} text ----\n".format(first["num"]))
    # Decode back for the preview (approx: replace non-print with '.')
    with open(first["raw_path"], "rb") as f:
        raw = f.read()
    nul = raw.find(b"\x00")
    txt = raw[:nul] if nul >= 0 else raw
    sys.stderr.write(txt.decode("latin-1", errors="replace"))
    sys.stderr.write("\n---- END TEXT ----\n")
    if first["sprite"]:
        sys.stderr.write("---- PREVIEW: moses_{:02d} sprite first 64 bytes ----\n"
                         .format(first["num"]))
        head = first["sprite"][:64]
        for i in range(0, len(head), 16):
            row = head[i:i + 16]
            hx = " ".join(f"{b:02x}" for b in row)
            asc = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
            sys.stderr.write(f"  {i:04x}: {hx:<48}  {asc}\n")
        sys.stderr.write(f"---- first opcode 0x{first['sprite'][0]:02x} "
                         f"(SPT_* index) ----\n")


if __name__ == "__main__":
    main()
