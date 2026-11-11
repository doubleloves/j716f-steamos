#!/bin/bash
# J716F: 用户设备上安装 Steam（发布版不随包分发 Valve 客户端）。
#
# 原则：
#   * 本脚本不包含任何 Valve 客户端文件；
#   * Steam 客户端由**用户自己的设备**在首启/首次运行时从 Valve 官方 CDN
#     或 Steam-ARM 社区安装器下载（等价于用户自己安装 Steam）；
#   * 需要网络（WiFi 必须先配好）。
#
# 自动/手动：
#   sudo j716f-install-steam.sh --auto      # 首启服务调用：无交互尝试安装，失败则记日志
#   sudo j716f-install-steam.sh            # 手动：交互式，可看到进度
#   sudo j716f-install-steam.sh --offline /path/steam-arm.tar.zst
#                                          # 用自己准备的离线包（可选）
#   sudo j716f-install-steam.sh --status   # 只看状态
#
# 安装成功后：写 /opt/steam-arm/.installed，恢复 Game Mode 默认会话并重启。
set -uo pipefail

MODE="manual"
OFFLINE=""
case "${1:-}" in
    --auto) MODE="auto" ;;
    --manual|"") MODE="manual" ;;
    --status) MODE="status" ;;
    --offline) MODE="offline"; OFFLINE="${2:-}" ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "未知参数: $1"; exit 2 ;;
esac

LOG=/var/log/j716f-steam-install.log
MARKER=/var/lib/j716f/steam-installed
INSTALLER=/usr/local/share/j716f/steam-arm-install.sh
BUNDLE_CACHE=/var/cache/j716f/steam-arm.tar.zst
KIOSK_USER=$(getent passwd 1000 | cut -d: -f1)
KIOSK_USER=${KIOSK_USER:-player}
KIOSK_HOME=$(getent passwd 1000 | cut -d: -f6)
KIOSK_HOME=${KIOSK_HOME:-/home/j716f}

say() {
    echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"
    for t in /dev/tty0 /dev/tty1 /dev/tty2; do
        [ -e "$t" ] && echo "$*" > "$t" 2>/dev/null
    done
}

steam_present() {
    # 兼容 Steam-ARM 的几种安装位置（系统级 / 用户级）
    [ -e "$MARKER" ] && return 0
    [ -x /usr/local/bin/steam ] && return 0
    [ -x /usr/local/bin/steam-arm ] && return 0
    [ -d "$KIOSK_HOME/.local/share/steam-arm" ] && return 0
    [ -d /opt/steam-arm ] && [ -n "$(ls -A /opt/steam-arm 2>/dev/null)" ] && return 0
    return 1
}

net_ok() {
    timeout 8 curl -fsSI https://store.steampowered.com >/dev/null 2>&1
}

if [ "$MODE" = "status" ]; then
    echo "steam_present: $(steam_present && echo yes || echo no)"
    echo "network:       $(net_ok && echo yes || echo no)"
    echo "marker:        $([ -e "$MARKER" ] && echo yes || echo no)"
    echo "installer:     $([ -x "$INSTALLER" ] && echo "$INSTALLER" || echo missing)"
    exit 0
fi

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }

say "== J716F Steam 安装器 ($MODE) =="
if steam_present; then
    say "Steam 已存在，无需安装"
    exit 0
fi
if ! net_ok; then
    say "错误: 没有网络。请先配网："
    say "  1) 接键盘保护套，Ctrl+Alt+F2 进 tty2 登录 $KIOSK_USER，运行 nmtui；或"
    say "  2) SD 卡 /j716f/wifi.txt 写 SSID=... 和 PSK=... 后重启，首启会自动配网。"
    exit 1
fi
say "网络正常"

# ---- 方式 A：用户自己准备的离线包（合法：用户从自己渠道获得） ----
if [ "$MODE" = "offline" ]; then
    [ -f "$OFFLINE" ] || { say "离线包不存在: $OFFLINE"; exit 1; }
    say "解压离线包到 /opt/steam-arm ..."
    mkdir -p /opt/steam-arm
    if ! tar --zstd -xf "$OFFLINE" -C /opt/steam-arm 2>/dev/null; then
        zstd -dc "$OFFLINE" | tar -xf - -C /opt/steam-arm || { say "解压失败"; exit 1; }
    fi
elif [ -f "$BUNDLE_CACHE" ]; then
    say "发现本地缓存 $BUNDLE_CACHE，解压 ..."
    mkdir -p /opt/steam-arm
    if ! tar --zstd -xf "$BUNDLE_CACHE" -C /opt/steam-arm 2>/dev/null; then
        zstd -dc "$BUNDLE_CACHE" | tar -xf - -C /opt/steam-arm || { say "解压失败"; exit 1; }
    fi
fi

# ---- 方式 B：社区安装器（Steam-ARM，运行时从 Valve CDN 拉客户端） ----
if ! steam_present; then
    if [ -x "$INSTALLER" ]; then
        say "运行 Steam-ARM 安装器（用户 $KIOSK_USER，客户端从 Valve CDN 下载）: $INSTALLER"
        # Steam-ARM 的安装器通常以普通用户身份安装到 ~/.local/share/steam-arm
        if id "$KIOSK_USER" >/dev/null 2>&1; then
            su - "$KIOSK_USER" -c "bash '$INSTALLER'" >/dev/null 2>&1 || \
                timeout 1800 su - "$KIOSK_USER" -c "bash '$INSTALLER'" >>"$LOG" 2>&1 || true
        else
            timeout 1800 "$INSTALLER" >>"$LOG" 2>&1 || true
        fi
    else
        say "未内置安装器脚本（发布版不包含 Valve 客户端）。请任选："
        say "  A. 手动安装 Steam-ARM（推荐，按项目 README 操作）："
        say "     git clone https://github.com/Scrumpper/Steam-ARM && cd Steam-ARM && sudo ./install.sh"
        say "  B. 自己下载 Steam-ARM 离线包后："
        say "     sudo $0 --offline /path/steam-arm.tar.zst"
        exit 1
    fi
fi

if ! steam_present; then
    say "安装未完成（安装器可能失败或需要交互）。请手动查看: tail -f $LOG"
    say "手动重试: sudo j716f-install-steam.sh"
    exit 1
fi

# ---- 安装成功后的收尾 ----
say "Steam 安装成功"
mkdir -p /var/lib/j716f
: > "$MARKER"
systemctl disable j716f-steam-install.service 2>/dev/null || true

# 退出 setup console 模式：恢复 greetd / 关掉自动登录 tty1
if [ -f /etc/j716f/steam-setup-mode ]; then
    systemctl unmask greetd 2>/dev/null || true
    rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf
    rm -rf /etc/systemd/system/getty@tty1.service.d
    systemctl disable getty@tty1.service 2>/dev/null || true
    systemctl daemon-reload 2>/dev/null || true
    say "已退出 setup console 模式，恢复 Game Mode"
fi

# 若之前为了 setup 模式改过 greetd，恢复 Game Mode（如果存在备份）
if [ -f /etc/greetd/config.toml.j716f-release ]; then
    cp -f /etc/greetd/config.toml.j716f-release /etc/greetd/config.toml
    say "已恢复 greetd Game Mode 配置"
fi
systemctl restart greetd 2>/dev/null || true
say "完成。Steam 账号需要你自己登录（脚本不包含任何账号）。"
say "如需重启生效: sudo systemctl reboot"
