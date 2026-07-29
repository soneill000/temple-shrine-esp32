#!/usr/bin/env python3
"""One-shot fetcher for bombergolf.cpp.z and bugbird.cpp.z from canewsin/templeos-1."""
import os, sys, urllib.request

TARGETS = [
    ("https://raw.githubusercontent.com/canewsin/templeos-1/master/iso/demo/games/bombergolf.cpp.z",
     "vendored/bombergolf.cpp.z"),
    ("https://raw.githubusercontent.com/canewsin/templeos-1/master/iso/demo/games/bugbird.cpp.z",
     "vendored/bugbird.cpp.z"),
]

here = os.path.dirname(os.path.abspath(__file__))
for url, rel in TARGETS:
    dst = os.path.join(here, rel)
    print(f"fetching {url} -> {dst}", file=sys.stderr)
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read()
    with open(dst, "wb") as f:
        f.write(data)
    print(f"  wrote {len(data)} bytes", file=sys.stderr)
