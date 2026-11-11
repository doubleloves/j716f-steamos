#!/usr/bin/env python3
"""Build a J716F card-flash payload directory (and optionally a recovery zip).

Two output forms:

1. SD / USB media layout (understood by the J716F mini-recovery, docs/84):
       <out>/j716f/SHA256SUMS
       <out>/j716f/boot.img
       <out>/j716f/rootfs.img.gz      (optional)
       <out>/j716f/manifest.json

2. TWRP-compatible zip (for a TWRP/recovery that runs update-binary):
       j716f-update.zip
         META-INF/com/google/android/update-binary
         META-INF/com/google/android/updater-script  (dummy)
         payload/...

The payload is the same, so one build can serve both routes.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--boot", help="boot.img (Android boot image)")
    ap.add_argument("--rootfs", help="rootfs image (.img.gz or .img), optional")
    ap.add_argument("--out", required=True, help="output directory (payload root)")
    ap.add_argument("--version", default="dev")
    ap.add_argument("--zip", help="also build a TWRP-compatible zip here")
    ap.add_argument("--update-binary",
                    default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "..", "tablet", "cardflash", "update-binary"),
                    help="update-binary file to embed in the zip")
    ap.add_argument("--qbootctl", help="static qbootctl to embed in payload")
    args = ap.parse_args()

    if not args.boot and not args.rootfs:
        sys.exit("need --boot and/or --rootfs")

    payloads = {}
    if args.boot:
        payloads["boot.img"] = args.boot
    if args.rootfs:
        base = os.path.basename(args.rootfs)
        if not (base.endswith(".img") or base.endswith(".img.gz")):
            sys.exit("rootfs must be .img or .img.gz")
        payloads["rootfs.img" if base.endswith(".img") else "rootfs.img.gz"] = args.rootfs
    if args.qbootctl:
        payloads["qbootctl"] = args.qbootctl

    jdir = os.path.join(args.out, "j716f")
    os.makedirs(jdir, exist_ok=True)
    for name, src in payloads.items():
        dst = os.path.join(jdir, name)
        print(f"copy {src} -> {dst}")
        shutil.copyfile(src, dst)

    sums = []
    for name in payloads:
        sums.append(f"{sha256(os.path.join(jdir, name))}  {name}")
    with open(os.path.join(jdir, "SHA256SUMS"), "w", encoding="utf-8") as f:
        f.write("\n".join(sums) + "\n")

    manifest = {
        "format": "j716f-cardflash-v1",
        "device": ["TB-J716F", "j716f", "p11_pro_plus"],
        "version": args.version,
        "files": [{"name": n, "size": os.path.getsize(os.path.join(jdir, n)),
                   "sha256": sha256(os.path.join(jdir, n))} for n in payloads],
        "notes": "rootfs.img.gz 会整盘覆盖 userdata（清空数据）；qbootctl 用 -m 标记成功，不用 -s。",
    }
    with open(os.path.join(jdir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
    print(f"payload ready: {jdir}")

    if args.zip:
        ub = os.path.abspath(args.update_binary)
        if not os.path.isfile(ub):
            sys.exit(f"update-binary not found: {ub}")

        def ctype(name):
            # 已压缩/大文件直接 store，避免二次压缩浪费时间和空间
            if name.endswith((".gz", ".img", ".simg", ".zst", ".xz", ".zip")):
                return zipfile.ZIP_STORED
            return zipfile.ZIP_DEFLATED

        with zipfile.ZipFile(args.zip, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(ub, "META-INF/com/google/android/update-binary")
            z.writestr("META-INF/com/google/android/updater-script", "# dummy\n")
            for name in payloads:
                z.write(os.path.join(jdir, name), f"payload/{name}",
                        compress_type=ctype(name))
            z.write(os.path.join(jdir, "SHA256SUMS"), "payload/SHA256SUMS")
            z.write(os.path.join(jdir, "manifest.json"), "payload/manifest.json")
        print(f"zip ready: {args.zip} ({os.path.getsize(args.zip)} bytes)")


if __name__ == "__main__":
    main()
