#!/bin/bash
# 在 J716F 上构建「发布版」rootfs 镜像（不修改当前系统，全部产物写到 SD 卡）。
#
# 用法（设备上，root）：
#   sudo ./build-rootfs.sh --inspect           # 先看磁盘占用/用户目录，决定清理项
#   sudo ./build-rootfs.sh                    # 默认 20G 镜像，产物在 /mnt/sdcard/j716f-release
#   sudo ./build-rootfs.sh --strip-steam      # 额外剔除 Steam/FEX（公开版的合规选择）
#   sudo ./build-rootfs.sh --size 24G --no-simg
#
# 产物：
#   <OUT>/rootfs.img.gz   —— 卡刷用（mini-recovery: /j716f/rootfs.img.gz）
#   <OUT>/rootfs.simg     —— 线刷用（fastboot flash userdata，Android sparse；--no-simg 可关）
#   <OUT>/boot.img        —— 当前 boot_a 的有效长度裁剪版
#   <OUT>/SHA256SUMS、manifest.txt
#
# 脚本只读取当前系统；所有写入都发生在新镜像/SD 卡上。
set -euo pipefail

ASSETS=${ASSETS:-/home/j716f/release-assets}
OUT=${OUT:-/mnt/sdcard/j716f-release}
SIZE=${SIZE:-20G}
STRIP_STEAM=0
SIMG=1
INSPECT=0
BOOT=1
while [ $# -gt 0 ]; do
    case "$1" in
        --inspect) INSPECT=1 ;;
        --strip-steam) STRIP_STEAM=1 ;;
        --no-simg) SIMG=0 ;;
        --no-boot) BOOT=0 ;;
        --size) SIZE="$2"; shift ;;
        --out) OUT="$2"; shift ;;
        *) echo "未知参数: $1"; exit 2 ;;
    esac
    shift
done

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }
[ -d "$ASSETS" ] || { echo "缺少 assets 目录: $ASSETS"; exit 1; }

if [ "$INSPECT" = 1 ]; then
    echo "== / 一级目录 =="; du -x -d1 -h / 2>/dev/null | sort -h | tail -20
    echo; echo "== /home/j716f =="; du -x -d1 -h /home/j716f 2>/dev/null | sort -h | tail -20
    echo; echo "== /opt =="; du -x -d1 -h /opt 2>/dev/null | sort -h | tail -10
    echo; echo "== /home/j716f 顶层文件 =="; ls -la /home/j716f | head -30
    echo; echo "== /home/j716f/.config（前 30 项） =="; ls -la /home/j716f/.config 2>/dev/null | head -30
    echo; echo "== 关键服务是否 enabled =="; systemctl is-enabled j716f-mark-success.service 2>/dev/null || true
    echo; echo "== SD 卡空间 =="; df -h /mnt/sdcard
    exit 0
fi

MNT=/mnt/release-root
IMG="$OUT/rootfs.img"
mkdir -p "$OUT" "$MNT"
df -h "$OUT" | tail -1

echo "== 1) 创建 ext4 镜像 ($SIZE) =="
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
mkfs.ext4 -L J716F-ROOT -m 1 -E lazy_itable_init=0,lazy_journal_init=0 "$IMG"

echo "== 2) 挂载并 rsync 当前系统 =="
mount -o loop "$IMG" "$MNT"
EXCLUDES=(
    /proc /sys /dev /run /tmp /mnt /media /lost+found
    /home/j716f /root /recovery.log /swapfile
    /var/log/journal /var/tmp /var/cache/pacman/pkg
    /var/lib/bluetooth /etc/NetworkManager/system-connections
    /etc/ssh "/etc/machine-id" /var/lib/dbus/machine-id
    "/var/lib/j716f-firstboot.done"
)
RSYNC_ARGS=()
for e in "${EXCLUDES[@]}"; do RSYNC_ARGS+=(--exclude="$e"); done
if [ "$STRIP_STEAM" = 1 ]; then
    echo "   --strip-steam: 排除 /opt/steam-arm 与 /opt/fex-rootfs"
    RSYNC_ARGS+=(--exclude=/opt/steam-arm --exclude=/opt/fex-rootfs)
fi
rsync -aHAX --numeric-ids "${RSYNC_ARGS[@]}" / "$MNT"/

echo "== 2.5) 用户名通用化：player -> player，家目录固定 /home/j716f =="
bash "$ASSETS/rename-user-in-image.sh" "$MNT"

echo "== 3) 写入发布版首启/清理资产 =="
install -m 0755 "$ASSETS/j716f-firstboot.sh" "$MNT/usr/local/sbin/j716f-firstboot.sh"
install -m 0755 "$ASSETS/j716f-user-setup.sh" "$MNT/usr/local/sbin/j716f-user-setup.sh"
install -m 0644 "$ASSETS/j716f-firstboot.service" "$MNT/etc/systemd/system/j716f-firstboot.service"
install -m 0644 "$ASSETS/j716f-boot-ok.service" "$MNT/etc/systemd/system/j716f-boot-ok.service"
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/j716f-firstboot.service "$MNT/etc/systemd/system/multi-user.target.wants/j716f-firstboot.service"
ln -sf /etc/systemd/system/j716f-boot-ok.service "$MNT/etc/systemd/system/multi-user.target.wants/j716f-boot-ok.service"

# 发布版系统内工具：重启进 recovery（卡刷入口）
install -m 0755 "$ASSETS/j716f-reboot-recovery.sh" "$MNT/usr/local/bin/j716f-reboot-recovery"
if [ -f "$ASSETS/write_bcb.py" ]; then
    install -m 0755 "$ASSETS/write_bcb.py" "$MNT/usr/local/sbin/j716f-write-bcb.py"
fi

# 首启自定义用户名/密码：profile 触发 + sudoers 放行 + pending 标记
install -m 0644 "$ASSETS/j716f-user-setup-profile.sh" "$MNT/etc/profile.d/99-j716f-user-setup.sh"
mkdir -p "$MNT/etc/j716f" "$MNT/etc/sudoers.d"
touch "$MNT/etc/j716f/user-setup-pending"
cat > "$MNT/etc/sudoers.d/05-j716f-setup" <<'EOF'
%wheel ALL=(root) NOPASSWD: /usr/local/sbin/j716f-user-setup.sh
EOF
chmod 0440 "$MNT/etc/sudoers.d/05-j716f-setup"
if [ -f "$ASSETS/j716f-mark-success.service" ]; then
    install -m 0644 "$ASSETS/j716f-mark-success.service" "$MNT/etc/systemd/system/j716f-mark-success.service"
    ln -sf /etc/systemd/system/j716f-mark-success.service "$MNT/etc/systemd/system/multi-user.target.wants/j716f-mark-success.service"
fi

if [ "$STRIP_STEAM" = 1 ]; then
    echo "   --strip-steam: 安装'用户自己装 Steam'的 setup 资产"
    mkdir -p "$MNT/etc/j716f" "$MNT/usr/local/sbin" "$MNT/usr/local/share/j716f"
    install -m 0755 "$ASSETS/steam/j716f-install-steam.sh" "$MNT/usr/local/sbin/j716f-install-steam.sh"
    install -m 0644 "$ASSETS/steam/j716f-steam-install.service" "$MNT/etc/systemd/system/j716f-steam-install.service"
    ln -sf /etc/systemd/system/j716f-steam-install.service "$MNT/etc/systemd/system/multi-user.target.wants/j716f-steam-install.service"
    touch "$MNT/etc/j716f/steam-setup-mode"
    # 如果资产里带了 Steam-ARM 安装器脚本（不含 Valve 客户端，仅下载器），一并装进去
    if [ -f "$ASSETS/steam/steam-arm-install.sh" ]; then
        install -m 0755 "$ASSETS/steam/steam-arm-install.sh" "$MNT/usr/local/share/j716f/steam-arm-install.sh"
        echo "   已内置 steam-arm-install.sh（运行时仍由用户设备从 Valve CDN 下载客户端）"
    else
        echo "   未内置 steam-arm-install.sh，用户需手动 git clone Steam-ARM 安装（README 有说明）"
    fi
    # 备用控制台：Ctrl+Alt+F2 进 tty2（键盘可用时）
    ln -sf /usr/lib/systemd/system/getty@.service "$MNT/etc/systemd/system/multi-user.target.wants/getty@tty2.service"
fi

echo "== 4) 清理个人数据 =="
bash "$ASSETS/sanitize.sh" "$MNT"

echo "== 5) 收起镜像 =="
umount "$MNT"
e2fsck -f -y "$IMG" >/dev/null
sync

if [ "$BOOT" = 1 ]; then
    echo "== 6) 导出 boot.img（当前 boot_a，裁剪到有效长度 + 去掉 gamemode mask） =="
    dd if=/dev/disk/by-partlabel/boot_a of="$OUT/boot-full.img" bs=1M status=none
    python3 "$ASSETS/trim_boot.py" "$OUT/boot-full.img" "$OUT/boot.img"
    rm -f "$OUT/boot-full.img"
    python3 "$ASSETS/fix_boot_cmdline.py" "$OUT/boot.img" "$OUT/boot.fixed.img"
    mv -f "$OUT/boot.fixed.img" "$OUT/boot.img"
fi

echo "== 7) 压缩（卡刷用） =="
rm -f "$OUT/rootfs.img.gz"
gzip -1 -c "$IMG" > "$OUT/rootfs.img.gz"
ls -lh "$OUT/rootfs.img.gz"

if [ "$SIMG" = 1 ]; then
    echo "== 8) 生成 Android sparse（线刷用） =="
    if command -v img2simg >/dev/null 2>&1; then
        rm -f "$OUT/rootfs.simg"
        img2simg "$IMG" "$OUT/rootfs.simg"
        ls -lh "$OUT/rootfs.simg"
    else
        echo "警告: 没有 img2simg（pacman -S android-tools），跳过 rootfs.simg"
    fi
fi

echo "== 9) 校验与清单 =="
(
    cd "$OUT"
    rm -f SHA256SUMS
    for f in boot.img rootfs.img.gz rootfs.simg; do
        [ -f "$f" ] && sha256sum "$f" >> SHA256SUMS
    done
    {
        echo "date=$(date -Is)"
        echo "kernel=$(uname -r)"
        echo "image_size=$SIZE"
        echo "strip_steam=$STRIP_STEAM"
        echo "packages=$(pacman -Q 2>/dev/null | wc -l)"
        echo "rootfs_img=$(stat -c%s "$IMG")"
        echo "rootfs_used=$(du -h --apparent-size "$IMG" | cut -f1)"
    } > manifest.txt
)
cat "$OUT/SHA256SUMS"
echo "产物目录: $OUT"
df -h "$OUT" | tail -1
