#!/usr/bin/env python3
"""J716F A/B slot attribute analyzer (read-only).

Qualcomm's A/B state lives in the GPT partition-entry attribute bits
(bits 48..63 of the 8-byte attributes field at entry offset 48), not in
the misc partition.  There are two layouts in the wild:

  * "new" (qbootctl / AOSP bootctrl HAL, hardware/qcom/bootctrl):
        attribute byte @ entry+54
          bit 2 (0x04) = slot active
          bit 6 (0x40) = boot successful
          bit 7 (0x80) = unbootable
  * "old" (hardware/bsp/qcom boot_control/gpt.h):
        bits 48..51 = priority
        bits 52..55 = tries remaining
        bit  56    = successful

This tool never writes anything.  Use it to reverse-engineer which
layout a device really uses: dump GPT heads before/after a `fastboot
set_active` or before/after a reboot and diff them.

Examples:
    ./gpt_slot_attrs.py /dev/sde
    ./gpt_slot_attrs.py --dump-dir backup/partitions
    ./gpt_slot_attrs.py --diff before/ after/
"""
import argparse
import glob
import json
import os
import struct
import sys

SECTOR512 = 512


def _read(path, offset, size):
    with open(path, "rb") as f:
        f.seek(offset)
        return f.read(size)


def read_gpt(path):
    """Return (sector_size, header, entries) for path (file or block dev)."""
    probe = _read(path, 0, 64 * 1024)
    off = probe.find(b"EFI PART")
    if off < 0:
        raise ValueError(f"{path}: no GPT header in first 64 KiB")
    sector = off  # LBA1 sits at offset == sector size
    if sector not in (512, 4096):
        # unusual sector size, still usable
        pass
    header = _read(path, sector, 92)
    if len(header) < 92:
        raise ValueError(f"{path}: short GPT header")
    (entries_lba,) = struct.unpack_from("<Q", header, 72)
    (num_entries,) = struct.unpack_from("<I", header, 80)
    (entry_size,) = struct.unpack_from("<I", header, 84)
    if entry_size < 128 or num_entries == 0 or num_entries > 4096:
        raise ValueError(f"{path}: bad GPT entry table ({num_entries}x{entry_size})")
    entries = _read(path, entries_lba * sector, num_entries * entry_size)
    return sector, header, [entries[i * entry_size:(i + 1) * entry_size]
                            for i in range(num_entries)]


def gpt_partitions(path):
    sector, header, entries = read_gpt(path)
    out = []
    for e in entries:
        if len(e) < 128 or e[:16] == b"\x00" * 16:
            continue
        name = e[56:128].decode("utf-16-le").rstrip("\x00")
        if not name:
            continue
        (attr,) = struct.unpack_from("<Q", e, 48)
        out.append({"name": name, "attr": attr, "path": path, "sector": sector})
    return out


def decode(attr):
    """Decode the layouts known to exist in the wild (pure, no I/O).

    The J716F layout below was confirmed on 2026-11-10 by diffing GPT
    attributes across a reboot and correlating with ABL's own log line
    "Active Slot _a is bootable, retry count N" (see docs/83):
      byte 54 = bits 0..2 priority/other | bits 3..5 retry count
                | bit 6 successful | bit 7 unbootable
    """
    b54 = (attr >> 48) & 0xFF
    return {
        "attr": attr,
        "raw_top16": (attr >> 48) & 0xFFFF,
        # confirmed on TB-J716F (Lenovo SM8250, ABL BOOT.XF.3.2)
        "j716f": {
            "byte54": b54,
            "retry": (b54 >> 3) & 0x7,
            "successful": bool(b54 & 0x40),
            "unbootable": bool(b54 & 0x80),
            "active": bool(b54 & 0x04),
        },
        # qbootctl / AOSP qcom bootctrl HAL layout
        "new": {
            "active": bool(b54 & 0x04),
            "successful": bool(b54 & 0x40),
            "unbootable": bool(b54 & 0x80),
            "byte54": b54,
        },
        # hardware/bsp/qcom boot_control/gpt.h layout
        "old": {
            "priority": (attr >> 48) & 0xF,
            "tries": (attr >> 52) & 0xF,
            "successful": bool((attr >> 56) & 1),
        },
    }


def summarize(parts):
    for p in parts:
        d = decode(p["attr"])
        n, o, j = d["new"], d["old"], d["j716f"]
        print("%-18s attr=0x%016x  j716f[active=%d retry=%d successful=%d unbootable=%d]  "
              "new[active=%d successful=%d unbootable=%d b54=0x%02x]  "
              "old[priority=%2d tries=%2d successful=%d]"
              % (p["name"], d["attr"], j["active"], j["retry"], j["successful"],
                 j["unbootable"], n["active"], n["successful"], n["unbootable"],
                 n["byte54"], o["priority"], o["tries"], o["successful"]))


def dump_dirs(dirs):
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, "*"))):
            if not os.path.isfile(f):
                continue
            try:
                parts = gpt_partitions(f)
            except Exception as exc:  # noqa: BLE001 - report and continue
                print(f"-- skip {f}: {exc}", file=sys.stderr)
                continue
            print(f"=== {f}")
            summarize(parts)


def collect(dirs):
    snap = {}
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d, "*"))):
            if not os.path.isfile(f):
                continue
            try:
                parts = gpt_partitions(f)
            except Exception:
                continue
            for p in parts:
                key = os.path.basename(f) + ":" + p["name"]
                snap[key] = p["attr"]
    return snap


def diff(a, b):
    old, new = collect([a]), collect([b])
    keys = sorted(set(old) | set(new))
    changed = 0
    for k in keys:
        if old.get(k) != new.get(k):
            changed += 1
            print("%-40s 0x%016x -> 0x%016x  %s"
                  % (k, old.get(k, 0), new.get(k, 0),
                     decode(new.get(k, 0))["new"]))
    print(f"\n{changed} attribute(s) changed", file=sys.stderr)
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device", nargs="?", help="block device or GPT head dump")
    ap.add_argument("--dump-dir", nargs="+", metavar="DIR",
                    help="analyze every file in these dirs")
    ap.add_argument("--diff", nargs=2, metavar=("DIR_OLD", "DIR_NEW"),
                    help="diff attribute snapshots between two dump dirs")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if args.diff:
        raise SystemExit(0 if diff(*args.diff) >= 0 else 1)
    if args.dump_dir:
        dump_dirs(args.dump_dir)
        return
    if not args.device:
        ap.error("need a device, --dump-dir or --diff")
    parts = gpt_partitions(args.device)
    if args.json:
        print(json.dumps([dict(p, **decode(p["attr"])) for p in parts],
                         indent=2, default=str))
    else:
        summarize(parts)


if __name__ == "__main__":
    main()
