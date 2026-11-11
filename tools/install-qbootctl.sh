#!/bin/bash
# 在 J716F 上构建并安装 qbootctl（Qualcomm A/B bootctl HAL 的 Linux 移植）。
#
# 目的：每个启动周期把当前槽标记为 "boot successful"，让 ABL 停止递减
# slot-retry-count（约 7 次重启后进 fastboot 的问题）。详见 docs/83。
#
# 用法（设备上，root）：
#   sudo ./install-qbootctl.sh              # 构建 + 只读验证（安全，不写 GPT）
#   sudo ./install-qbootctl.sh --service    # 另外安装并启用开机服务
#   sudo ./install-qbootctl.sh --salsa      # 从 salsa 源克隆（GitHub 慢/被墙时用）
#
# 安全说明：
#   * 本脚本不会执行 `qbootctl -m/-s/-u`；只构建 + dump。
#   * 真正写 GPT 的动作留给 docs/83 的验证流程，确认布局后再开服务。
set -euo pipefail

MODE_SERVICE=0
SRC="github"
for a in "$@"; do
  case "$a" in
    --service) MODE_SERVICE=1 ;;
    --salsa)   SRC="salsa" ;;
    *) echo "未知参数: $a"; exit 2 ;;
  esac
done

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }
command -v pacman >/dev/null || { echo "不是 Arch 系统"; exit 1; }

echo "== 1) 安装构建依赖 =="
pacman -S --needed --noconfirm git meson ninja gcc pkgconf zlib cmake

echo "== 2) 取源码 =="
SRCDIR=/root/qbootctl-src
rm -rf "$SRCDIR"
if [ "$SRC" = "salsa" ]; then
  git clone --depth 1 https://salsa.debian.org/DebianOnMobile-team/qbootctl.git "$SRCDIR"
else
  git clone --depth 1 https://github.com/linux-msm/qbootctl.git "$SRCDIR" \
    || git clone --depth 1 https://salsa.debian.org/DebianOnMobile-team/qbootctl.git "$SRCDIR"
fi

echo "== 3) 构建 =="
cd "$SRCDIR"
meson setup build --buildtype=release
meson compile -C build
BIN=$(find build -maxdepth 2 -type f -name qbootctl -perm -u+x | head -1)
[ -n "$BIN" ] || { echo "找不到编译产物"; exit 1; }
install -m 0755 "$BIN" /usr/local/sbin/qbootctl
echo "已安装: /usr/local/sbin/qbootctl"

echo "== 4) 只读验证（不写 GPT） =="
echo "--- qbootctl 当前解读 ---"
/usr/local/sbin/qbootctl || true
echo
echo "--- /proc/cmdline 中的 slot ---"
tr ' ' '\n' < /proc/cmdline | grep -i 'slot' || true
echo
echo "（可选）在 PC 上执行 fastboot getvar all，确认 ABL 侧的"
echo "  slot-retry-count / slot-successful / slot-unbootable 与 qbootctl 输出是否一致"

if [ "$MODE_SERVICE" -eq 1 ]; then
  echo "== 5) 安装开机标记服务 =="
  cat > /etc/systemd/system/j716f-mark-success.service <<'EOF'
[Unit]
Description=Mark current Qualcomm A/B slot successful (J716F)
ConditionPathExists=/usr/local/sbin/qbootctl
ConditionPathExistsGlob=/dev/disk/by-partlabel/boot_*
After=local-fs.target
Before=shutdown.target
Conflicts=shutdown.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/qbootctl -m
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload
  systemctl enable j716f-mark-success.service
  echo "已启用 j716f-mark-success.service（下次启动开始每启动执行 qbootctl -m）"
  echo "验证: systemctl status j716f-mark-success; journalctl -u j716f-mark-success -b"
  echo "警告: 先按 docs/83 §3 完成 1 次手动 qbootctl -m + 10 次重启验证，再长期启用！"
fi

echo "== 完成 =="
