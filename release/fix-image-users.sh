#!/bin/bash
# 修补已构建镜像里的用户名残留（player 组名、旧 uid 1001 的 /run/user 路径、passwd 备份）。
# 用法: sudo ./fix-image-users.sh [OUT_DIR]
set -euo pipefail

OUT=${1:-/mnt/sdcard/j716f-release}
M=/mnt/fiximg
OLD_UID=1001

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }
[ -f "$OUT/rootfs.img" ] || { echo "缺少 $OUT/rootfs.img"; exit 1; }

umount "$M" 2>/dev/null || true
mkdir -p "$M"
LOOP=$(losetup -f --show "$OUT/rootfs.img")
mount -o rw "$LOOP" "$M"

echo "== 1) 修复用户名/uid 残留 =="
rm -f "$M"/etc/passwd- "$M"/etc/shadow- "$M"/etc/group- "$M"/etc/gshadow-
CHANGED=0
while IFS= read -r -d '' f; do
    case "$f" in
        */etc/passwd|*/etc/group|*/etc/shadow|*/etc/gshadow|*/etc/services|*/etc/rpc) continue ;;
    esac
    grep -Iq . "$f" 2>/dev/null || continue
    if grep -qE "\byp\b|/run/user/$OLD_UID" "$f" 2>/dev/null; then
        sed -i -E "s|/run/user/$OLD_UID|/run/user/1000|g; s/\byp\b/player/g" "$f"
        CHANGED=$((CHANGED+1))
        echo "  ~ ${f#$M}"
    fi
done < <(find "$M/etc" "$M/usr/local" "$M/home/j716f" "$M/var/lib/j716f" -type f -print0 2>/dev/null)
echo "  修改 $CHANGED 个文件"

echo "== 2) 复查 =="
grep -rIn -w player "$M/etc/systemd" "$M/usr/local/bin" "$M/usr/local/sbin" 2>/dev/null | head -10 || true
grep -rIn "run/user/$OLD_UID" "$M/etc" "$M/usr/local" 2>/dev/null | head -10 || true
grep player "$M/etc/passwd" || true

umount "$M"
losetup -d "$LOOP" 2>/dev/null || true
e2fsck -f -y "$OUT/rootfs.img" >/dev/null

echo "== 3) 重新生成 gz / simg / hashes =="
gzip -1 -c "$OUT/rootfs.img" > "$OUT/rootfs.img.gz"
img2simg "$OUT/rootfs.img" "$OUT/rootfs.simg"
(
    cd "$OUT"
    rm -f SHA256SUMS
    for f in boot.img rootfs.img.gz rootfs.simg; do
        [ -f "$f" ] && sha256sum "$f" >> SHA256SUMS
    done
    echo "user_fix=player->player,uid1001->1000" >> manifest.txt
)
cat "$OUT/SHA256SUMS"
