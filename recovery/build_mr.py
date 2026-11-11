#!/usr/bin/env python3
"""Build the J716F mini-recovery boot image.

Takes a working Android boot image (best: the current boot_a dump), replaces
the initramfs init with the mini-recovery init, adds /recovery.sh and a static
qbootctl, then repacks the boot image unchanged otherwise (same kernel/DTB).

Run on the tablet (or on PC against a dumped boot.img):

    python3 build_mr.py --base base.img --out boot-minirecovery.img \
        --files-dir ./mini-recovery --qbootctl ./qbootctl-static

Then flash with:
    sudo ./install-mini-recovery.sh /home/j716f/deploy/boot-minirecovery.img
"""
import argparse
import gzip
import os
import struct
import sys

S_IFREG = 0o100000
S_IFDIR = 0o040000
S_IFLNK = 0o120000


def parse_boot(d):
    assert d[:8] == b"ANDROID!", "not an Android boot image"
    (ks, ka, rs, ra, ss, sa, ts, ps, hv, ov) = struct.unpack_from("<10I", d, 8)
    if hv < 2:
        raise SystemExit("only boot image header v2 supported")
    dtbsz = struct.unpack_from("<I", d, 1648)[0]
    off = ps
    kernel = d[off:off + ks]; off += align(ks, ps)
    ramdisk = d[off:off + rs]; off += align(rs, ps)
    second = d[off:off + ss]; off += align(ss, ps)
    dtb = d[off:off + dtbsz]
    return dict(hdr=d[:ps], kernel=kernel, ramdisk=ramdisk, second=second,
                dtb=dtb, page=ps, dtb_addr=struct.unpack_from("<Q", d, 1652)[0])


def align(x, p):
    return x + (-x % p)


def parse_cpio(dec):
    entries = []
    i = 0
    while i + 110 <= len(dec):
        hdr = dec[i:i + 110]
        if hdr[:6] not in (b"070701", b"070702"):
            raise SystemExit(f"bad cpio magic at {i}: {hdr[:6]!r}")
        fields = [int(hdr[6 + 8 * n:14 + 8 * n], 16) for n in range(13)]
        (ino, mode, uid, gid, nlink, mtime, fsize, devmaj, devmin,
         rdevmaj, rdevmin, namesize, check) = fields
        name = dec[i + 110:i + 110 + namesize - 1].decode("utf-8", "replace")
        ds = (i + 110 + namesize + 3) & ~3
        content = dec[ds:ds + fsize]
        entries.append(dict(name=name, mode=mode, uid=uid, gid=gid,
                            nlink=nlink, mtime=mtime, content=content,
                            devmaj=devmaj, devmin=devmin,
                            rdevmaj=rdevmaj, rdevmin=rdevmin))
        i = ds + fsize
        i = (i + 3) & ~3
        if name == "TRAILER!!!":
            break
    return entries


def newc(entries):
    out = bytearray()
    ino = 1
    for e in entries:
        name = e["name"].encode()
        content = e["content"]
        hdr = bytearray(b"070701")
        vals = [ino, e["mode"], e["uid"], e["gid"], e["nlink"], e["mtime"],
                len(content), e["devmaj"], e["devmin"], e["rdevmaj"],
                e["rdevmin"], len(name) + 1, 0]
        hdr += b"".join(b"%08x" % v for v in vals)
        assert len(hdr) == 110, len(hdr)
        out += hdr + name + b"\x00"
        out += b"\x00" * ((-len(hdr) - len(name) - 1) % 4)
        out += content
        out += b"\x00" * ((-len(content)) % 4)
        ino += 1
    return bytes(out)


def add_file(entries, path, content, mode=S_IFREG | 0o755):
    entries.append(dict(name=path, mode=mode, uid=0, gid=0, nlink=1,
                        mtime=0, content=content, devmaj=0, devmin=0,
                        rdevmaj=0, rdevmin=0))


def build(base_path, out_path, files_dir, qbootctl_path, extras=()):
    d = open(base_path, "rb").read()
    boot = parse_boot(d)
    dec = gzip.decompress(boot["ramdisk"])
    entries = parse_cpio(dec)

    # drop old init/trailer, remember order
    trailer = entries[-1] if entries[-1]["name"] == "TRAILER!!!" else None
    entries = [e for e in entries if e["name"] not in ("init", "TRAILER!!!")]

    init_bytes = open(os.path.join(files_dir, "init"), "rb").read()
    rec_bytes = open(os.path.join(files_dir, "recovery.sh"), "rb").read()
    add_file(entries, "init", init_bytes)
    add_file(entries, "recovery.sh", rec_bytes)
    if qbootctl_path:
        add_file(entries, "bin/qbootctl", open(qbootctl_path, "rb").read())
    for item in extras:
        name, src = item.split("=", 1)
        add_file(entries, name, open(src, "rb").read())

    if trailer is None:
        add_file(entries, "TRAILER!!!", b"", S_IFREG)
    else:
        entries.append(trailer)
    entries[-1]["content"] = b""
    entries[-1]["mode"] = S_IFREG

    cpio = newc(entries)
    ram = gzip.compress(cpio, compresslevel=9, mtime=0)
    print(f"new ramdisk: cpio={len(cpio)} gzip={len(ram)}")

    ps = boot["page"]
    hdr = bytearray(boot["hdr"])
    struct.pack_into("<I", hdr, 8, len(boot["kernel"]))
    struct.pack_into("<I", hdr, 16, len(ram))
    struct.pack_into("<I", hdr, 24, len(boot["second"]))
    struct.pack_into("<I", hdr, 36, ps)
    struct.pack_into("<I", hdr, 1648, len(boot["dtb"]))
    struct.pack_into("<Q", hdr, 1652, boot["dtb_addr"])

    def pad(b):
        return b + b"\x00" * ((-len(b)) % ps)

    body = (bytes(hdr) + pad(boot["kernel"]) + pad(ram) +
            pad(boot["second"]) + pad(boot["dtb"]))
    open(out_path, "wb").write(body)
    print(f"wrote {out_path}: {len(body)} bytes (kernel {len(boot['kernel'])}, "
          f"dtb {len(boot['dtb'])})")

    # --- self verify ---
    v = open(out_path, "rb").read()
    vb = parse_boot(v)
    vdec = gzip.decompress(vb["ramdisk"])
    ventries = parse_cpio(vdec)
    names = {e["name"] for e in ventries}
    need = {"init", "recovery.sh"}
    if qbootctl_path:
        need.add("bin/qbootctl")
    missing = need - names
    if missing:
        raise SystemExit(f"verify failed: missing {sorted(missing)}")
    print("verify ok: entries=%d names=%s" % (len(ventries), sorted(need)))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="working boot.img (e.g. dd of boot_a)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--files-dir", required=True, help="dir with init and recovery.sh")
    ap.add_argument("--qbootctl", default="", help="static qbootctl binary (optional)")
    ap.add_argument("--extra", action="append", default=[],
                    metavar="NAME=PATH", help="extra file to add to initramfs")
    args = ap.parse_args()
    qb = args.qbootctl if args.qbootctl and os.path.isfile(args.qbootctl) else ""
    if args.qbootctl and not qb:
        print(f"warning: qbootctl not found: {args.qbootctl}", file=sys.stderr)
    build(args.base, args.out, args.files_dir, qb, args.extra)


if __name__ == "__main__":
    main()
