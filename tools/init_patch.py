#!/usr/bin/env python3
"""Patch the custom initramfs init script.

1. Root device preference (mode=rescue|main).
2. Boot-success watchdog: after 3 boots of a rootfs without /.boot-ok, skip it and
   fall back to the next candidate device (a service in the OS creates /.boot-ok).
"""
import sys

path = sys.argv[1]
mode = sys.argv[2]
s = open(path).read()

old_root = ("for dev in /dev/sda18 /dev/sd*18 /dev/disk/by-partlabel/userdata "
            "/dev/sda* /dev/mmcblk0p* ; do")
if old_root not in s:
    raise SystemExit(f"root pattern not found in {path}")
if mode == "rescue":
    new_root = ("for dev in /dev/sda17 /dev/sd*17 /dev/sda18 /dev/sd*18 "
                "/dev/disk/by-partlabel/userdata /dev/sda* /dev/mmcblk0p* ; do")
elif mode == "main":
    new_root = ("for dev in /dev/sda18 /dev/sd*18 /dev/sda17 /dev/sd*17 "
                "/dev/disk/by-partlabel/userdata /dev/sda* /dev/mmcblk0p* ; do")
else:
    raise SystemExit("mode must be rescue|main")
s = s.replace(old_root, new_root)

old_mark = ('        if [ -x /newroot/sbin/init ] || [ -x /newroot/lib/systemd/systemd ]; then\n'
            '            echo "rootfs found on $dev -> switch_root"')
new_mark = ('        if [ -x /newroot/sbin/init ] || [ -x /newroot/lib/systemd/systemd ]; then\n'
            '            cnt=0\n'
            '            [ -f /newroot/.boot-count ] && cnt=$(cat /newroot/.boot-count 2>/dev/null || echo 0)\n'
            '            case "$cnt" in \'\'|*[!0-9]*) cnt=0 ;; esac\n'
            '            cnt=$((cnt+1))\n'
            '            echo "$cnt" > /newroot/.boot-count 2>/dev/null\n'
            '            if [ ! -f /newroot/.boot-ok ] && [ "$cnt" -ge 3 ]; then\n'
            '                echo "!! $dev has no .boot-ok after $cnt boots, falling back"\n'
            '                umount /newroot 2>/dev/null\n'
            '                continue\n'
            '            fi\n'
            '            echo "rootfs found on $dev -> switch_root"')
if old_mark not in s:
    raise SystemExit(f"marker pattern not found in {path}")
s = s.replace(old_mark, new_mark)
open(path, "w").write(s)
print(f"patched {path} ({mode})")
