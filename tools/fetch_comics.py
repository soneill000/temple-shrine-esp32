#!/usr/bin/env python3
"""
fetch_comics.py -- pull Terry Davis's Moses comics from the canewsin/templeos-1
mirror. Numbering has gaps (no 03/09/11/33/...); we probe sequentially and keep
the first 10 that exist.

Despite the `.z` suffix on the source files, the payloads are stored as raw
plaintext DolDoc followed by a binary CDocBin tail (same 16-byte header format
as extract_sprite_tail.py handles). We write them out as `moses_NN.raw`.

Idempotent: if `moses_NN.raw` already exists locally, we skip that number
(but still count it toward the target-of-10).

USAGE
    py fetch_comics.py [--target 10] [--max-probe 60] [--out-dir DIR]
"""

import argparse
import os
import sys
import urllib.request
import urllib.error


BASE_URL = ("https://raw.githubusercontent.com/canewsin/templeos-1/"
            "master/iso/apps/afteregypt/comics/moses{:02d}.txt.z")


def fetch_one(num: int, out_dir: str) -> tuple[bool, int, str]:
    """Return (exists, size_bytes, path_or_reason).
    exists=True with size=0 means 'downloaded empty' (unlikely for real files).
    exists=False means 404 or other HTTP error."""
    out_path = os.path.join(out_dir, f"moses_{num:02d}.raw")
    if os.path.exists(out_path):
        sz = os.path.getsize(out_path)
        return True, sz, out_path + " (cached)"
    url = BASE_URL.format(num)
    req = urllib.request.Request(url, headers={"User-Agent": "shrine-fetch/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
    except urllib.error.HTTPError as e:
        return False, 0, f"HTTP {e.code}"
    except urllib.error.URLError as e:
        return False, 0, f"URL error: {e}"
    with open(out_path, "wb") as f:
        f.write(data)
    return True, len(data), out_path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target", type=int, default=10,
                    help="how many comics to collect (default 10)")
    ap.add_argument("--max-probe", type=int, default=60,
                    help="highest moses NN we'll try (default 60)")
    ap.add_argument("--out-dir", default=None,
                    help="directory to write moses_NN.raw into "
                         "(default: comics_out next to this script)")
    args = ap.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir = args.out_dir or os.path.join(script_dir, "comics_out")
    os.makedirs(out_dir, exist_ok=True)

    got = []
    missing = []
    for n in range(1, args.max_probe + 1):
        if len(got) >= args.target:
            break
        ok, sz, info = fetch_one(n, out_dir)
        if ok:
            got.append((n, sz, info))
            sys.stderr.write(f"  moses{n:02d}: {sz:>7} bytes  -> {info}\n")
        else:
            missing.append(n)
            sys.stderr.write(f"  moses{n:02d}: skip ({info})\n")

    sys.stderr.write(f"\nfetched {len(got)}/{args.target} comic(s) "
                     f"into {out_dir}\n")
    if missing:
        sys.stderr.write(f"gaps in numbering: {missing}\n")
    if len(got) < args.target:
        sys.stderr.write(f"warning: only got {len(got)} of {args.target} "
                         f"requested (raise --max-probe?)\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
