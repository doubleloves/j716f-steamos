#!/bin/bash
# 在已挂载的发布版 rootfs 上执行清理（被 build-rootfs.sh 调用）。
# 用法: sudo ./sanitize.sh /mnt/r
set -euo pipefail

ROOT=${1:?usage: sanitize.sh <mounted-root>}
[ -d "$ROOT/etc" ] || { echo "不是 rootfs: $ROOT"; exit 1; }

KIOSK_USER=$(awk -F: '$3==1000{print $1}' "$ROOT/etc/passwd" | head -1)
KIOSK_USER=${KIOSK_USER:-player}
echo "== 清理个人数据（用户: $KIOSK_USER） =="

# 用户目录：清空内容，保留骨架
rm -rf "$ROOT"/home/j716f/* "$ROOT"/home/j716f/.[!.]* "$ROOT"/home/j716f/..?* 2>/dev/null || true
mkdir -p "$ROOT/home/j716f/.config" "$ROOT/home/j716f/.local/share" "$ROOT/home/j716f/Desktop"
chown -R 1000:1000 "$ROOT/home/j716f" 2>/dev/null || true

# root、日志、缓存、临时
rm -rf "$ROOT"/root/* "$ROOT"/root/.[!.]* 2>/dev/null || true
rm -rf "$ROOT"/var/log/* "$ROOT"/var/tmp/* "$ROOT"/tmp/* 2>/dev/null || true
rm -rf "$ROOT"/var/lib/bluetooth/* 2>/dev/null || true
rm -rf "$ROOT"/etc/NetworkManager/system-connections/* 2>/dev/null || true
rm -rf "$ROOT"/var/cache/pacman/pkg/* 2>/dev/null || true
rm -f "$ROOT"/swapfile "$ROOT"/recovery.log 2>/dev/null || true
rm -f "$ROOT"/.boot-count "$ROOT"/.boot-ok 2>/dev/null || true
rm -f "$ROOT"/etc/ssh/ssh_host_* 2>/dev/null || true

# 调试用 netconsole（含个人电脑 IP/MAC，不该进发布版）
rm -f "$ROOT"/usr/local/bin/j716f-netconsole.sh "$ROOT"/usr/local/bin/*netconsole* 2>/dev/null || true
rm -f "$ROOT"/etc/systemd/system/*netconsole* "$ROOT"/etc/modules-load.d/netconsole* \
      "$ROOT"/etc/modprobe.d/netconsole* 2>/dev/null || true

# 设备身份
: > "$ROOT/etc/machine-id"
rm -f "$ROOT/var/lib/dbus/machine-id" 2>/dev/null || true
printf 'j716f\n' > "$ROOT/etc/hostname"

# 密码：默认 $KIOSK_USER / j716f（首启 setup 会让用户改）
if [ -x "$ROOT/usr/bin/chpasswd" ] || [ -x "$ROOT/bin/chpasswd" ]; then
    chroot "$ROOT" /bin/sh -c "echo '$KIOSK_USER:j716f' | chpasswd" 2>/dev/null || echo "警告: 设置默认密码失败"
    chroot "$ROOT" /bin/sh -c "chage -M 99999 -I -1 '$KIOSK_USER'" 2>/dev/null || true
fi

# fstab：根用 LABEL，去掉 swapfile 行
if [ -f "$ROOT/etc/fstab" ]; then
    sed -i -E 's|^[^#[:space:]]+[[:space:]]+/[[:space:]]+ext4|LABEL=J716F-ROOT / ext4|' "$ROOT/etc/fstab"
    sed -i '/swapfile/d' "$ROOT/etc/fstab"
fi

echo "== 抽查残留 =="
if grep -rIl -E 'CHANGEME|SERIALNUMBER|192\.168\.|BEGIN OPENSSH PRIVATE KEY' \
    "$ROOT/etc/j716f" "$ROOT/etc/NetworkManager" "$ROOT/usr/local" "$ROOT/home/j716f" 2>/dev/null | head -5; then
    echo "警告: 上面文件可能含个人信息，请人工确认（可用 grep -n 查看）"
else
    echo "抽查通过"
fi

echo "== 清理完成（用户: $KIOSK_USER） =="
