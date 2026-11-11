#!/usr/bin/env python3
"""Extract the ramdisk (initramfs) from an Android boot.img on the tablet."""
import gzip
import os
import struct
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "/tmp/boot_a.raw"
out = sys.argv[2] if len(sys.argv) > 2 else "/home/j716f/deploy/ramdisk.base.cpio.gz"

d = open(src, "rb").read()
assert d[:8] == b"ANDROID!", d[:8]
(ksz, kaddr, rsz, raddr, ssz, saddr, tags, page, hv, osv) = struct.unpack_from("<10I", d, 8)
print(f"kernel={ksz} ramdisk={rsz} page={page}")
off = page + (ksz + page - 1) // page * page
ramdisk = d[off:off + rsz]
print("ramdisk magic:", ramdisk[:2].hex())
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "wb").write(ramdisk)

# sanity: decompress
try:
    raw = gzip.decompress(ramdisk)
    print(f"decompressed {len(raw)} bytes, cpio magic {raw[:6]!r}")
except Exception as e:
    print("decompress failed:", e)
    sys.exit(1)
