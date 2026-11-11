#!/bin/bash
# 在 J716F 上一键构建 mini-recovery 镜像（可选：构建后写入 recovery_a/b）。
#
# 用法（设备上，root）：
#   sudo ./make-mini-recovery.sh              # 只构建 -> /home/j716f/deploy/boot-minirecovery.img
#   sudo ./make-mini-recovery.sh --flash      # 构建并写入 recovery_a/recovery_b
#
# 前置：
#   /home/j716f/mini-recovery/{init,recovery.sh,build_mr.py,extract_file.py}
#   /root/qbootctl-src（qbootctl 源码，用于构建 static 二进制）
#   若 /root/mr/base.img 不存在，会自动 dd 当前 boot_a。
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }

MODE_FLASH=0
[ "${1:-}" = "--flash" ] && MODE_FLASH=1

BASE=/root/mr/base.img
OUT=/home/j716f/deploy/boot-minirecovery.img
FILES=/home/j716f/mini-recovery
QSRC=/root/qbootctl-src
QB=$QSRC/build-static/qbootctl

echo "== 1) 准备 base.img（当前 boot_a） =="
mkdir -p /root/mr /home/j716f/deploy
if [ ! -s "$BASE" ]; then
    dd if=/dev/disk/by-partlabel/boot_a of="$BASE" bs=1M status=none
    echo "已 dump boot_a -> $BASE"
else
    echo "复用已有 $BASE"
fi

echo "== 2) 静态 qbootctl =="
if [ ! -x "$QB" ]; then
    cd "$QSRC"
    rm -rf build-static
    meson setup build-static -Dc_args=-static -Dc_link_args=-static >/tmp/qb-static-setup.log 2>&1
    meson compile -C build-static >/tmp/qb-static-build.log 2>&1
fi
[ -x "$QB" ] || { echo "qbootctl 静态构建失败，见 /tmp/qb-static-*.log"; exit 1; }
file "$QB" | grep -q "statically linked" || echo "警告: $QB 不是静态链接"

echo "== 3) 生成 by-partlabel 映射（initramfs 无 udev） =="
: > "$FILES/by-partlabel.map"
for f in /dev/disk/by-partlabel/*; do
    [ -e "$f" ] || continue
    echo "$(basename "$f") $(readlink -f "$f")" >> "$FILES/by-partlabel.map"
done
wc -l "$FILES/by-partlabel.map"

echo "== 4) 构建 boot-minirecovery.img =="
cd "$FILES"
python3 build_mr.py --base "$BASE" --out "$OUT" --files-dir "$FILES" \
    --qbootctl "$QB" --extra etc/by-partlabel.map="$FILES/by-partlabel.map"
sha256sum "$OUT"

if [ "$MODE_FLASH" -eq 1 ]; then
    echo "== 5) 写入 recovery_a/recovery_b =="
    bash /home/j716f/tools/install-mini-recovery.sh "$OUT"
    echo "完成。进入 recovery: sudo python3 /home/j716f/tools/write_bcb.py --command boot-recovery"
else
    echo "完成（未刷写）。刷写: sudo ./make-mini-recovery.sh --flash"
fi
