#!/bin/busybox sh
# J716F mini-recovery main script.
#
# Payload layout on SD card / USB stick (FAT32 or ext4):
#   /j716f/SHA256SUMS      (sha256sum 格式，相对 /j716f)
#   /j716f/boot.img        (可选，Android boot 镜像，会写入 boot_a+boot_b)
#   /j716f/rootfs.img.gz   (可选，ext4 镜像 gz，会整盘写入 userdata)
#   /j716f/HOLD            (可选，存在则执行后停在 shell，不自动重启)
#
# 安全约定：
#   * 只有 SHA256 校验通过才会写任何分区；
#   * 没找到 payload 时只写日志、20 秒后自动重启回正常系统；
#   * rootfs 只有显式提供 rootfs.img.gz 时才覆盖（会清空用户数据）。
#
# 日志：/mnt/data/recovery.log（userdata 能挂载时），同时输出到 tty0/控制台。

export PATH=/bin:/sbin:/usr/bin:/usr/sbin
LOG=/mnt/data/recovery.log
BOOT_A=/dev/sde11
BOOT_B=/dev/sde34
USERDATA=/dev/sda18
QBOOTCTL=/bin/qbootctl

log() {
    msg="[$(date +%H:%M:%S 2>/dev/null)] $*"
    echo "$msg"
    echo "$msg" >> "$LOG" 2>/dev/null
    for t in /dev/tty0 /dev/tty1 /dev/console; do
        [ -e "$t" ] && echo "$msg" > "$t" 2>/dev/null
    done
}

# ---------------------------------------------------------------- USB gadget
setup_gadget() {
    G=/sys/kernel/config/usb_gadget/g1
    mount -t configfs none /sys/kernel/config 2>/dev/null
    [ -d /sys/kernel/config/usb_gadget ] || return 0
    mkdir -p "$G" 2>/dev/null
    echo 0x1d6b > "$G/idVendor" 2>/dev/null
    echo 0x0104 > "$G/idProduct" 2>/dev/null
    echo 0x0200 > "$G/bcdUSB" 2>/dev/null
    echo 0x0100 > "$G/bcdDevice" 2>/dev/null
    mkdir -p "$G/strings/0x409" 2>/dev/null
    echo "0123456789" > "$G/strings/0x409/serialnumber" 2>/dev/null
    echo "j716f" > "$G/strings/0x409/manufacturer" 2>/dev/null
    echo "Lenovo Xiaoxin Pad Pro 2021" > "$G/strings/0x409/product" 2>/dev/null
    mkdir -p "$G/configs/c.1/strings/0x409" 2>/dev/null
    echo "acm+rndis" > "$G/configs/c.1/strings/0x409/configuration" 2>/dev/null
    echo 500 > "$G/configs/c.1/MaxPower" 2>/dev/null
    mkdir -p "$G/functions/acm.usb0" "$G/functions/rndis.usb0" 2>/dev/null
    echo "AA:BB:CC:DD:EE:FF" > "$G/functions/rndis.usb0/dev_addr" 2>/dev/null
    echo "AA:BB:CC:DD:EE:FF" > "$G/functions/rndis.usb0/host_addr" 2>/dev/null
    ln -sf "$G/functions/acm.usb0"   "$G/configs/c.1/" 2>/dev/null
    ln -sf "$G/functions/rndis.usb0" "$G/configs/c.1/" 2>/dev/null
    UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
    [ -n "$UDC" ] && echo "$UDC" > "$G/UDC" 2>/dev/null
    i=0
    while [ "$i" -lt 20 ]; do
        [ -e /sys/class/net/usb0 ] && break
        sleep 0.2; i=$((i+1))
    done
    if [ -e /sys/class/net/usb0 ]; then
        ip link set usb0 up 2>/dev/null
        ip addr add 172.16.42.1/24 dev usb0 2>/dev/null
        log "USB RNDIS: 172.16.42.1/24"
    fi
    [ -c /dev/ttyGS0 ] && setsid sh -c 'exec sh </dev/ttyGS0 >/dev/ttyGS0 2>&1' &
    telnetd -l /bin/sh -p 23 2>/dev/null
}

# ------------------------------------------------------------- mount userdata
mount_userdata() {
    mkdir -p /mnt/data
    for dev in "$USERDATA" /dev/sda18 /dev/sd?18; do
        [ -b "$dev" ] || continue
        mount -t ext4 -o rw "$dev" /mnt/data 2>/dev/null && { log "userdata mounted: $dev"; return 0; }
    done
    log "userdata 挂载失败，日志只输出到控制台"
    return 1
}

# --------------------------------------------------------------- find payload
MEDIA=""
find_media() {
    mkdir -p /mnt/src
    mount -t tmpfs tmpfs /mnt/src 2>/dev/null
    for dev in /dev/mmcblk0p* /dev/mmcblk1p* /dev/sd[g-z]* /dev/sd[g-z]; do
        [ -b "$dev" ] || continue
        base=$(basename "$dev")
        case "$base" in
            sd[a-f]*) continue ;;   # 内部 UFS LUN，绝不当作更新介质
        esac
        for fs in vfat exfat ext4; do
            if mount -t "$fs" -o ro "$dev" /mnt/src 2>/dev/null; then
                if [ -d /mnt/src/j716f ]; then
                    MEDIA=/mnt/src
                    log "找到更新介质: $dev ($fs)"
                    return 0
                fi
                umount /mnt/src 2>/dev/null
            fi
        done
    done
    return 1
}

# 手工校验（不用 busybox sha256sum -c，兼容旧 busybox）
verify_payload() {
    cd "$MEDIA/j716f" || return 1
    [ -f SHA256SUMS ] || { log "缺少 SHA256SUMS，拒绝刷写"; return 1; }
    log "校验 payload ..."
    while read -r want name; do
        case "$want" in ''|\#*) continue ;; esac
        [ -f "$name" ] || { log "缺少文件: $name"; return 1; }
        got=$(busybox sha256sum "$name" 2>/dev/null | busybox awk '{print $1}')
        if [ "$got" != "$want" ]; then
            log "校验失败: $name (want $want got $got)"
            return 1
        fi
    done < SHA256SUMS
    log "校验通过"
    return 0
}

flash_boot() {
    [ -f boot.img ] || return 0
    magic=$(busybox head -c 8 boot.img 2>/dev/null)
    if [ "$magic" != "ANDROID!" ]; then
        log "boot.img 不是 Android boot 镜像，跳过"
        return 1
    fi
    log "写入 boot_a / boot_b ..."
    busybox dd if=boot.img of="$BOOT_A" bs=1M 2>/dev/null || { log "写 boot_a 失败"; return 1; }
    busybox dd if=boot.img of="$BOOT_B" bs=1M 2>/dev/null || { log "写 boot_b 失败"; return 1; }
    return 0
}

flash_rootfs() {
    [ -f rootfs.img.gz ] || return 0
    log "警告: 即将整盘覆盖 userdata（清空数据），10 秒后开始 ..."
    sleep 10
    busybox gunzip -c rootfs.img.gz | busybox dd of="$USERDATA" bs=1M 2>/dev/null \
        || { log "写 userdata 失败"; return 1; }
    return 0
}

mark_success() {
    [ -x "$QBOOTCTL" ] || { log "qbootctl 不存在，跳过标记"; return 0; }
    prepare_partlabel
    "$QBOOTCTL" -m >> "$LOG" 2>&1 && log "已标记当前槽 successful"
}

# initramfs 里没有 udev，qbootctl 依赖 /dev/disk/by-partlabel；按静态映射建符号链接。
prepare_partlabel() {
    mkdir -p /dev/disk/by-partlabel 2>/dev/null
    if [ -f /etc/by-partlabel.map ]; then
        while read -r name dev; do
            case "$name" in ''|\#*) continue ;; esac
            [ -b "$dev" ] && ln -sf "$dev" "/dev/disk/by-partlabel/$name"
        done < /etc/by-partlabel.map
    else
        # 兜底：至少让 qbootctl 能数槽/标记
        ln -sf /dev/sde11 /dev/disk/by-partlabel/boot_a 2>/dev/null
        ln -sf /dev/sde34 /dev/disk/by-partlabel/boot_b 2>/dev/null
        ln -sf /dev/sde17 /dev/disk/by-partlabel/dtbo_a 2>/dev/null
        ln -sf /dev/sde40 /dev/disk/by-partlabel/dtbo_b 2>/dev/null
    fi
}

clear_bcb() {
    # 清掉 misc 里的 bootloader_message command（前 32 字节），
    # 否则某些 ABL 会在每次开机都重新进入 recovery。
    busybox dd if=/dev/zero of=/dev/sda3 bs=1 count=32 conv=fsync 2>/dev/null \
        && log "BCB command cleared"
}

# ---------------------------------------------------------------------- main
setup_gadget
mount_userdata
clear_bcb          # 尽早清 BCB，避免任何异常导致反复进 recovery

log "mini-recovery start; kernel=$(uname -r 2>/dev/null)"
log "boot args: $(cat /proc/cmdline 2>/dev/null)"
busybox lsblk >> "$LOG" 2>/dev/null

APPLIED=0
if find_media; then
    if verify_payload; then
        if flash_boot; then
            APPLIED=1
            if flash_rootfs; then
                APPLIED=1
            fi
        fi
        mark_success
        sync
    fi
else
    log "没有找到 /j716f payload（SD 卡/U 盘根目录）"
fi

if [ -e "$MEDIA/j716f/HOLD" ]; then
    log "检测到 HOLD 文件，停在 shell（telnet/串口可用），不自动重启"
    exec sh
fi

if [ "$APPLIED" -eq 1 ]; then
    log "更新完成，3 秒后重启"
    sleep 3
else
    log "无事可做，20 秒后重启回正常系统"
    sleep 20
fi
clear_bcb
busybox sync
busybox reboot -f 2>/dev/null || echo b > /proc/sysrq-trigger
