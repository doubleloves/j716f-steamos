#!/usr/bin/env python3
"""Extract a file from a boot image's initramfs (e.g. bin/busybox)."""
import gzip
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_mr  # noqa: E402

if len(sys.argv) != 4:
    sys.exit("usage: extract_file.py boot.img name-in-cpio outpath")

boot = build_mr.parse_boot(open(sys.argv[1], "rb").read())
entries = build_mr.parse_cpio(gzip.decompress(boot["ramdisk"]))
for e in entries:
    if e["name"] == sys.argv[2]:
        with open(sys.argv[3], "wb") as f:
            f.write(e["content"])
        print(f"wrote {sys.argv[3]} ({len(e['content'])} bytes)")
        break
else:
    sys.exit(f"not found: {sys.argv[2]}")
