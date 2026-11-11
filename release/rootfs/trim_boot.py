#!/usr/bin/env python3
"""Trim a dumped boot partition to the valid Android boot-image length."""
import struct
import sys

src, dst = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()
assert d[:8] == b"ANDROID!", "not a boot image"
(ks, ka, rs, ra, ss, sa, ta, ps, hv, ov) = struct.unpack_from("<10I", d, 8)
dtbsz = struct.unpack_from("<I", d, 1648)[0]
a = lambda x: (x + ps - 1) // ps * ps  # noqa: E731
total = ps + a(ks) + a(rs) + a(ss) + a(dtbsz)
open(dst, "wb").write(d[:total])
print(f"trimmed {len(d)} -> {total} bytes")
