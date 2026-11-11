#!/bin/bash
# 首个用户登录时运行：让用户自定义登录名 + 密码（家目录固定 /home/j716f）。
# 由 /etc/profile.d/j716f-user-setup.sh 通过 sudo 触发（仅限 uid 1000 用户）。
set -euo pipefail

FLAG=/etc/j716f/user-setup-pending
NEWHOME=/home/j716f
CUR=$(getent passwd 1000 | cut -d: -f1)
[ -n "$CUR" ] || { echo "找不到 uid 1000 用户"; exit 1; }
[ -e "$FLAG" ] || { echo "无需初始化用户"; exit 0; }
[ "$(id -u)" -eq 0 ] || { echo "需要 root"; exit 1; }

echo ""
echo "======================================================="
echo "  J716F 首次设置：自定义登录用户名和密码"
echo "  当前占位用户名: $CUR"
echo "  （登录名可自定义，家目录固定 $NEWHOME，不影响使用）"
echo "======================================================="
echo ""

read -r -p "新用户名（回车默认 player）: " NEW
NEW=${NEW:-player}
case "$NEW" in
    [a-z]*[a-z0-9_-]*) ;;
    *) echo "用户名只能小写字母开头，包含小写字母/数字/_-"; exit 1 ;;
esac
if [ "$NEW" != "$CUR" ] && getent passwd "$NEW" >/dev/null 2>&1; then
    echo "用户 $NEW 已存在"; exit 1
fi

read -r -s -p "设置密码: " P1; echo
read -r -s -p "再输一次: " P2; echo
[ -n "$P1" ] && [ "$P1" = "$P2" ] || { echo "两次密码不一致或为空"; exit 1; }

echo "== 应用 =="
if [ "$NEW" != "$CUR" ]; then
    groupmod -n "$NEW" "$CUR" 2>/dev/null || true
    usermod -l "$NEW" -d "$NEWHOME" "$CUR"
fi
echo "$NEW:$P1" | chpasswd
usermod -aG wheel "$NEW" 2>/dev/null || true

# 更新 systemd/greetd/脚本里的用户名与家目录
FILE_GLOBS=(--include=*.service --include=*.timer --include=*.target --include=*.sh
            --include=*.conf --include=*.toml --include=*.desktop --include=*.rules
            --include=*.py --include=*.json --include=*.env --include=*.profile)
while IFS= read -r -d '' f; do
    sed -i -E "s|/home/$CUR|$NEWHOME|g; s/(^|[^A-Za-z0-9_])$CUR([^A-Za-z0-9_]|$)/\1$NEW\2/g" "$f" 2>/dev/null || true
done < <(find /etc /usr/local "$NEWHOME" /var/lib/j716f \
              -type f "${FILE_GLOBS[@]}" -print0 2>/dev/null)

# sudoers：允许新用户使用 sudo（setup 脚本本身只在首启用一次）
cat > /etc/sudoers.d/10-j716f-user <<EOF
$NEW ALL=(ALL:ALL) ALL
EOF
chmod 0440 /etc/sudoers.d/10-j716f-user 2>/dev/null || true

# 启动时自动登录的配置（tty1 autologin）
if [ -f /etc/systemd/system/getty@tty1.service.d/autologin.conf ]; then
    sed -i "s/--autologin $CUR/--autologin $NEW/" /etc/systemd/system/getty@tty1.service.d/autologin.conf 2>/dev/null || true
fi

rm -f "$FLAG"
systemctl daemon-reload 2>/dev/null || true
echo ""
echo "完成：登录名=$NEW，家目录=$NEWHOME"
echo "请重启：sudo reboot"
