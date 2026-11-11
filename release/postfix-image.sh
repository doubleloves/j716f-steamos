#!/bin/bash
# 构建后修补：从已构建的 rootfs.img 里再清掉调试残留（netconsole 里含个人 IP/MAC），
# 然后重新生成 rootfs.img.gz / rootfs.simg / SHA256SUMS。
# 用法: sudo ./postfix-image.sh [OUT_DIR]
set -euo pipefail

OUT=${1:-/mnt/sdcard/j716f-release}
M=/mnt/fiximg

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }
[ -f "$OUT/rootfs.img" ] || { echo "缺少 $OUT/rootfs.img"; exit 1; }

umount "$M" 2>/dev/null || true
mkdir -p "$M"
mount -o loop "$OUT/rootfs.img" "$M"

echo "== 1) 删除 netconsole 调试残留 =="
rm -f "$M"/usr/local/bin/j716f-netconsole.sh "$M"/usr/local/bin/*netconsole* 2>/dev/null || true
rm -f "$M"/etc/systemd/system/*netconsole* "$M"/etc/modprobe.d/netconsole* \
      "$M"/etc/modules-load.d/netconsole* 2>/dev/null || true

echo "== 2) 个人信息扫描（应无输出） =="
grep -rIl -E 'CHANGEME|SERIALNUMBER|192\.168\.|BEGIN OPENSSH PRIVATE KEY' \
    "$M/etc" "$M/usr/local" "$M/home" 2>/dev/null | head -10 || true

echo "== 3) 用户/家目录检查 =="
grep -E '^(player|player|alarm):' "$M/etc/passwd" || true
stat -c '%u:%g %n' "$M/home/j716f" 2>/dev/null || true
grep -E '^LABEL=J716F-ROOT' "$M/etc/fstab" || true
ls "$M/etc/j716f" 2>/dev/null || true

echo "== 4) 残留用户名扫描（应无输出） =="
grep -rIl -E '(^|[^A-Za-z0-9_])player([^A-Za-z0-9_]|$)' \
    "$M/etc/systemd" "$M/usr/local/bin" "$M/usr/local/sbin" 2>/dev/null | head -10 || true

umount "$M"
e2fsck -f -y "$OUT/rootfs.img" >/dev/null

echo "== 5) 重新生成 rootfs.img.gz =="
gzip -1 -c "$OUT/rootfs.img" > "$OUT/rootfs.img.gz"

echo "== 6) 重新生成 rootfs.simg =="
img2simg "$OUT/rootfs.img" "$OUT/rootfs.simg"

echo "== 7) 重新生成 SHA256SUMS / manifest =="
(
    cd "$OUT"
    rm -f SHA256SUMS
    for f in boot.img rootfs.img.gz rootfs.simg; do
        [ -f "$f" ] && sha256sum "$f" >> SHA256SUMS
    done
    cat >> manifest.txt <<EOF
postfix=netconsole-removed
postfix_date=$(date -Is)
EOF
)
ls -lh "$OUT"
cat "$OUT/SHA256SUMS"
