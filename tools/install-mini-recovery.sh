#!/bin/bash
# 把 J716F 的 mini-recovery 安装到 recovery_a / recovery_b（从 Linux 内，无需 PC）。
#
# 背景：recovery_a/recovery_b 各 100 MiB，用与 boot_a 相同的 Android boot.img
# 格式。装好后可以用 `misc` 的 BCB command "boot-recovery" 从系统内直接重启
# 进 recovery（配合 tools/write_bcb.py），或尝试按键组合进入。
#
# 用法：
#   sudo ./install-mini-recovery.sh [镜像路径] [--reboot]
#   默认镜像: /home/j716f/deploy/boot-recovery.img
#
# 注意：会覆盖原厂 recovery（原本也没有用）。装前自动备份到
#       /home/j716f/backup/recovery_a-before-<ts>.img 与 recovery_b 同理。
set -euo pipefail

IMG=${1:-/home/j716f/deploy/boot-recovery.img}
REBOOT=0
for a in "$@"; do [ "$a" = "--reboot" ] && REBOOT=1; done
[ -f "$IMG" ] || { echo "镜像不存在: $IMG"; exit 1; }
[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }

# 校验 boot 镜像魔数
head -c 8 "$IMG" | grep -q 'ANDROID!' || { echo "不是 Android boot 镜像: $IMG"; exit 1; }
SZ=$(stat -c%s "$IMG")
echo "镜像: $IMG ($SZ bytes)"

TS=$(date +%Y%m%d-%H%M%S)
for part in recovery_a recovery_b; do
  DEV=/dev/disk/by-partlabel/$part
  [ -b "$DEV" ] || { echo "找不到 $part"; exit 1; }
  CAP=$(blockdev --getsize64 "$DEV")
  echo "$part: 容量 $CAP bytes"
  [ "$SZ" -le "$CAP" ] || { echo "镜像大于 $part 分区，拒绝写入"; exit 1; }

  BAK=/home/j716f/backup/${part}-before-$TS.img
  mkdir -p /home/j716f/backup
  echo "  备份 $part -> $BAK"
  dd if="$DEV" of="$BAK" bs=1M status=none conv=fsync

  echo "  写入 $part"
  dd if="$IMG" of="$DEV" bs=4M status=progress conv=fsync
  sync
done

echo "完成。recovery_a/recovery_b 已写入。"
if [ "$REBOOT" -eq 1 ]; then
  echo "准备重启到 recovery（BCB command=boot-recovery）"
  python3 /home/j716f/tools/write_bcb.py --command boot-recovery
else
  echo "以后要进入 recovery:  sudo python3 /home/j716f/tools/write_bcb.py --command boot-recovery"
  echo "或尝试按键组合（音量加 + 电源，待实测）"
fi
