#!/usr/bin/env python3
"""Strip leftover systemd.mask=j716f-gamemode* tokens from a boot.img cmdline.

The nightly debugging image had `systemd.mask=j716f-gamemode.service` in its
cmdline so Steam would not autostart; a release image must not carry that.
Header id is left untouched (unlocked ABL does not verify it).
"""
import struct
import sys

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, "rb").read())
assert d[:8] == b"ANDROID!", "not a boot image"
(ks, ka, rs, ra, ss, sa, ta, ps, hv, ov) = struct.unpack_from("<10I", d, 8)
assert hv == 2, "expect header v2"

old = d[64:64 + 512].split(b"\x00")[0].decode("utf-8", "replace")
tokens = [t for t in old.split() if "systemd.mask=j716f-gamemode" not in t]
new = " ".join(tokens)
d[64:64 + 512] = new.encode() + b"\x00" * (512 - len(new))
open(dst, "wb").write(d)
print("old cmdline:", old)
print("new cmdline:", new)
