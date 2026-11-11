#!/usr/bin/env python3
"""Write a command into the Android bootloader control block (misc @ offset 0).

This is the standard AOSP mechanism the bootloader uses to decide to boot
recovery instead of the normal system.  We use it to reboot into our own
mini-recovery (recovery_a/recovery_b) without fastboot and without a PC.

  sudo ./write_bcb.py --command boot-recovery
  sudo ./write_bcb.py --command boot-recovery --no-reboot   # just stage it

Safety:
  * only the 32-byte `command` field is written; the rest of the 2 KiB
    bootloader_message and the partition are left untouched
  * the first 4 KiB of misc is backed up to /root/misc-head-backup-<ts>.bin
  * --restore <file> puts a backup back
"""
import argparse
import datetime
import os
import sys

MISC = "/dev/disk/by-partlabel/misc"
CMD_FIELD_SIZE = 32
BACKUP_DIR = "/root"


def read_head(path, size=4096):
    with open(path, "rb") as f:
        return f.read(size)


def write_head(path, data):
    with open(path, "r+b") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--command", default="boot-recovery",
                    help="BCB command string (32 bytes max), default boot-recovery")
    ap.add_argument("--misc", default=MISC)
    ap.add_argument("--restore", metavar="FILE",
                    help="restore a previous misc-head backup and exit")
    ap.add_argument("--no-reboot", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.misc):
        sys.exit(f"misc partition not found: {args.misc}")

    head = read_head(args.misc)
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

    if args.restore:
        data = open(args.restore, "rb").read()
        if len(data) != 4096:
            sys.exit(f"{args.restore}: expected 4096 bytes, got {len(data)}")
        if not args.dry_run:
            write_head(args.misc, data)
            print(f"restored {args.restore} -> {args.misc}")
        else:
            print(f"[dry-run] would restore {args.restore}")
        return

    cmd = args.command.encode()
    if len(cmd) > CMD_FIELD_SIZE:
        sys.exit("command too long")

    old_cmd = head[:CMD_FIELD_SIZE].split(b"\x00")[0]
    print(f"current BCB command: {old_cmd!r}")
    print(f"new     BCB command: {cmd!r}")

    backup = os.path.join(BACKUP_DIR, f"misc-head-backup-{ts}.bin")
    if not args.dry_run:
        with open(backup, "wb") as f:
            f.write(head)
        print(f"backup: {backup}")

        new_head = bytearray(head)
        new_head[:CMD_FIELD_SIZE] = cmd + b"\x00" * (CMD_FIELD_SIZE - len(cmd))
        write_head(args.misc, bytes(new_head))

        check = read_head(args.misc, CMD_FIELD_SIZE).split(b"\x00")[0]
        print(f"readback: {check!r}")
        if check != cmd:
            sys.exit("readback mismatch!")
    else:
        print("[dry-run] not writing")

    if not args.no_reboot and not args.dry_run:
        print("rebooting in 2s ... (Ctrl-C to cancel)")
        os.system("sleep 2; systemctl reboot")


if __name__ == "__main__":
    main()
