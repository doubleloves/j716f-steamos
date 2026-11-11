#!/bin/bash
# 发布版首启脚本（在镜像内以 root 运行一次）。
# 职责：扩分区、重置设备身份、启动 setup console（如需自定义用户名/装 Steam）。
set -e

ROOTDEV=$(findmnt -n -o SOURCE /)
echo "firstboot: rootdev=$ROOTDEV"
resize2fs "$ROOTDEV" || echo "firstboot: resize2fs skipped"

[ -s /etc/machine-id ] || systemd-machine-id-setup || true
ssh-keygen -A || true
hostnamectl set-hostname j716f || true

# 让 initramfs 的 .boot-ok 看门狗满意
touch /.boot-ok
rm -f /.boot-count

KIOSK_USER=$(getent passwd 1000 | cut -d: -f1)
[ -n "$KIOSK_USER" ] || KIOSK_USER=player

# 可选：SD 卡 /j716f/wifi.txt（SSID=... / PSK=...）自动配网
for m in /mnt/sdcard /run/media/*/*; do
    [ -f "$m/j716f/wifi.txt" ] || continue
    ssid=$(sed -n 's/^SSID=//p' "$m/j716f/wifi.txt" | head -1)
    psk=$(sed -n 's/^PSK=//p' "$m/j716f/wifi.txt" | head -1)
    if [ -n "$ssid" ]; then
        nmcli connection delete j716f-setup 2>/dev/null || true
        nmcli connection add type wifi con-name j716f-setup ifname wlp1s0 ssid "$ssid" \
            wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$psk" autoconnect yes 2>/dev/null \
            && nmcli connection up j716f-setup 2>/dev/null || true
        echo "firstboot: wifi configured from $m/j716f/wifi.txt"
    fi
done

# 打印默认密码提示到控制台
for t in /dev/tty1 /dev/tty0; do
    [ -e "$t" ] && {
        echo "" > "$t"
        echo "J716F 首次启动。默认密码: j716f（首次设置里会要求你自定义用户名/密码）。" > "$t"
        echo "配网：接键盘后 Ctrl+Alt+F2 进 tty2，运行 nmtui；或在 SD 卡 /j716f/wifi.txt 预置。" > "$t"
    } 2>/dev/null
done

# ---- 决定是否需要进入 setup console ----
SETUP=0
if [ -f /etc/j716f/user-setup-pending ]; then
    SETUP=1
    REASON="自定义用户名/密码"
elif [ -f /etc/j716f/steam-setup-mode ] && [ ! -e /var/lib/j716f/steam-installed ]; then
    SETUP=1
    REASON="安装 Steam"
fi

if [ "$SETUP" -eq 1 ]; then
    echo "firstboot: 进入 setup console（$REASON）"
    systemctl mask greetd 2>/dev/null || true
    systemctl stop greetd 2>/dev/null || true
    mkdir -p /etc/systemd/system/getty@tty1.service.d
    cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $KIOSK_USER --noclear %I \$TERM
EOF
    systemctl daemon-reload || true
    systemctl enable getty@tty1.service 2>/dev/null || true
    systemctl start getty@tty1.service 2>/dev/null || true
    for t in /dev/tty1 /dev/tty0; do
        [ -e "$t" ] && {
            echo "" > "$t"
            echo "==============================================" > "$t"
            echo "  J716F 首次设置：$REASON" > "$t"
            if [ -f /etc/j716f/user-setup-pending ]; then
                echo "  按提示输入新的登录用户名和密码。" > "$t"
                echo "  （家目录固定 /home/j716f，不影响使用）" > "$t"
            fi
            if [ -f /etc/j716f/steam-setup-mode ] && [ ! -e /var/lib/j716f/steam-installed ]; then
                echo "  配网: nmtui   装 Steam: sudo j716f-install-steam.sh" > "$t"
                echo "  完成后: sudo reboot" > "$t"
            fi
            echo "==============================================" > "$t"
        } 2>/dev/null
    done
fi

touch /var/lib/j716f-firstboot.done
