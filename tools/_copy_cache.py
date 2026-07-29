#!/usr/bin/env python3
"""Copy webfetch cache bins into vendored/ under sensible names."""
import shutil, os, sys, glob

CACHE = r"C:\Users\navi\.claude\projects\c--Users-navi-Projects-DefconBadge2026\f23f647b-8d54-4164-b674-e2a953b8b880\tool-results"
DST = r"C:\Users\navi\Projects\DefconBadge2026\firmware\templeshrine\tools\vendored"

MAP = {
    "webfetch-1785204199326-9r7eui.bin": "bombergolf.cpp.z",
    "webfetch-1785204215685-jq2q2t.bin": "bugbird.cpp.z",
}

for src_name, dst_name in MAP.items():
    src = os.path.join(CACHE, src_name)
    dst = os.path.join(DST, dst_name)
    if not os.path.exists(src):
        print(f"MISSING: {src}", file=sys.stderr)
        continue
    shutil.copyfile(src, dst)
    print(f"copied {src_name} -> {dst_name} ({os.path.getsize(dst)} bytes)")
