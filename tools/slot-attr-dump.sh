#!/bin/bash
# J716F A/B slot attribute snapshot (READ-ONLY).
#
# Saves the GPT header + partition entries of every UFS LUN (sda..sdf) and
# decodes the Qualcomm A/B flags in the partition attributes (bits 48..63).
# Use it before/after `fastboot set_active`, before/after a reboot, or
# before/after running qbootctl, then diff the directories on the PC with
# tools/gpt_slot_attrs.py --diff.
#
# Usage:  sudo ./slot-attr-dump.sh [OUTDIR]
# Output: OUTDIR/sdX-gpt-head.bin  (header sector + entries, same format as
#         D:\xiaoxinpadpro\backup\partitions\*-gpt-head.bin)
#         OUTDIR/attrs.txt         (decoded table + /proc/cmdline + misc head)
set -u

OUT=${1:-/root/slotattr-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT" || exit 1
echo "输出目录: $OUT"

# --- 1) 环境信息 ---
{
  echo "== date =="; date -Is
  echo "== uname =="; uname -a
  echo "== cmdline =="; cat /proc/cmdline
  echo
  echo "== lsblk =="; lsblk -o NAME,SIZE,FSTYPE,PARTLABEL,LABEL 2>/dev/null
  echo
  echo "== by-partlabel (boot/recovery/xbl/abl) =="
  ls -l /dev/disk/by-partlabel/ 2>/dev/null | grep -E 'boot_|recovery|xbl|abl|misc'
  echo
  echo "== qbootctl (if installed) =="
  if command -v qbootctl >/dev/null 2>&1; then
    qbootctl 2>&1 || true
  else
    echo "qbootctl not installed"
  fi
  echo
  echo "== misc 前 4 KiB 十六进制（BCB：0..2K = bootloader_message） =="
  dd if=/dev/disk/by-partlabel/misc bs=4096 count=1 2>/dev/null | od -A x -t x1z -v | head -12
} > "$OUT/attrs.txt" 2>&1

# --- 2) 每个 LUN 的 GPT（header + entries） ---
python3 - "$OUT" <<'PY'
import os, struct, sys

out = sys.argv[1]
disks = [f"sd{c}" for c in "abcdef"]
names = []

for dev in disks:
    path = "/dev/" + dev
    if not os.path.exists(path):
        continue
    # 找 GPT header（LBA1 = 偏移 512 或 4096）
    with open(path, "rb") as f:
        head = f.read(65536)
    off = head.find(b"EFI PART")
    if off <= 0:
        continue
    sector = off
    hdr = head[off:off + 92]
    (entries_lba,) = struct.unpack_from("<Q", hdr, 72)
    (num,) = struct.unpack_from("<I", hdr, 80)
    (esz,) = struct.unpack_from("<I", hdr, 84)
    with open(path, "rb") as f:
        f.seek(entries_lba * sector)
        entries = f.read(num * esz)
    if len(entries) != num * esz:
        print(f"{dev}: short read", file=sys.stderr)
        continue
    fn = os.path.join(out, f"{dev}-gpt-head.bin")
    with open(fn, "wb") as f:
        # 与 PC 上 *-gpt-head.bin 相同的约定：前面铺到 sector 偏移，再放 92 字节 header，
        # 然后 entries。tools/gpt_slot_attrs.py 只依赖 header 里的 entries_lba/num/esz，
        # 所以这里直接写 "0 填充 + header + entries" 即可。
        f.write(b"\x00" * sector)
        f.write(hdr)
        f.write(b"\x00" * (entries_lba * sector - sector - 92))
        f.write(entries)
    names.append((dev, sector, num, esz, fn))
    print(f"{dev}: sector={sector} entries={num}x{esz} -> {fn}")

if not names:
    print("没有找到任何 GPT", file=sys.stderr)
    raise SystemExit(1)

# 3) 直接在这里解码一份，便于手机上直接看
print("\n== A/B attributes ==")
print("%-18s %-18s %-8s %-8s %-8s %-8s %-8s" %
      ("partition", "attr", "new_act", "new_succ", "new_nboot", "old_prio", "old_tries"))
for dev, sector, num, esz, fn in names:
    with open(fn, "rb") as f:
        f.seek(0)
        f.read(sector)
        hdr = f.read(92)
        (entries_lba,) = struct.unpack_from("<Q", hdr, 72)
        f.seek(entries_lba * sector)
        entries = f.read(num * esz)
    for i in range(num):
        e = entries[i * esz:(i + 1) * esz]
        if len(e) < 128 or e[:16] == b"\x00" * 16:
            continue
        name = e[56:128].decode("utf-16-le").rstrip("\x00")
        if not name:
            continue
        (attr,) = struct.unpack_from("<Q", e, 48)
        if attr == 0 and not name.startswith(("boot_", "recovery")):
            continue
        b54 = (attr >> 48) & 0xFF
        print("%-18s 0x%016x %-8d %-8d %-8d %-8d %-8d" %
              (name, attr, bool(b54 & 0x04), bool(b54 & 0x40), bool(b54 & 0x80),
               (attr >> 48) & 0xF, (attr >> 52) & 0xF))
PY

# --- 4) 备份 misc 头（BCB 当前内容） ---
dd if=/dev/disk/by-partlabel/misc of="$OUT/misc-head.bin" bs=4096 count=1 2>/dev/null
echo
echo "完成: $OUT"
ls -la "$OUT"
echo
echo "把整个目录拷回 PC 后执行:"
echo "  python tools/gpt_slot_attrs.py --dump-dir <DIR>"
echo "  python tools/gpt_slot_attrs.py --diff <旧DIR> <新DIR>"
