#!/bin/bash
# 构建时把镜像里的旧用户名 player 换成通用占位用户名 player，
# 并把家目录统一到 /home/j716f（用户之后可在首启自定义登录名，家目录保持不变）。
#
# 额外处理：Arch Linux ARM 默认自带 alarm(uid 1000) 账户；本机会话用户 player 是 uid 1001。
# 为了统一，本脚本删除未使用的 alarm，并把 kiosk 用户规范到 uid/gid 1000。
#
# 用法: sudo ./rename-user-in-image.sh /mnt/release-root
set -euo pipefail

ROOT=${1:?usage: rename-user-in-image.sh <mounted-root>}
OLD=player
NEW=player
NEWHOME=/home/j716f

[ -f "$ROOT/etc/passwd" ] || { echo "不是 rootfs: $ROOT"; exit 1; }

OLD_UID=$(awk -F: -v u="$OLD" '$1==u{print $3}' "$ROOT/etc/passwd")
OLD_GID=$(awk -F: -v u="$OLD" '$1==u{print $4}' "$ROOT/etc/passwd")
[ -n "$OLD_UID" ] || { echo "错误: $ROOT/etc/passwd 里没有 $OLD"; exit 1; }
echo "旧用户 $OLD: uid=$OLD_UID gid=$OLD_GID"

echo "== 1) 移除未使用的 uid 1000 账户 =="
PLACE=$(awk -F: '$3==1000 && $1!="'"$OLD"'" {print $1}' "$ROOT/etc/passwd" | head -1)
if [ -n "$PLACE" ]; then
    echo "  删除占位账户: $PLACE"
    sed -i "/^$PLACE:/d" "$ROOT/etc/passwd" "$ROOT/etc/shadow" "$ROOT/etc/group" "$ROOT/etc/gshadow" 2>/dev/null || true
    sed -i -E "s/:$PLACE,/:/g; s/,$PLACE,/,/g; s/,$PLACE\$//; s/:$PLACE\$//" "$ROOT/etc/group" "$ROOT/etc/gshadow" 2>/dev/null || true
    rm -rf "$ROOT/home/$PLACE" 2>/dev/null || true
fi

echo "== 2) 把 kiosk 用户规范到 uid/gid 1000 =="
if [ "$OLD_UID" != "1000" ]; then
    sed -i -E "s|^$OLD:([^:]*):$OLD_UID:$OLD_GID:([^:]*):/home/$OLD:|$OLD:\1:1000:1000:\2:/home/$OLD:|" "$ROOT/etc/passwd"
    sed -i -E "s|^$OLD:x:$OLD_GID:|$OLD:x:1000:|" "$ROOT/etc/group"
fi

echo "== 3) 改 passwd/group/shadow + 家目录 =="
sed -i -E "s|^$OLD:([^:]*):[0-9]+:[0-9]+:([^:]*):/home/$OLD:|$NEW:\1:1000:1000:\2:$NEWHOME:|" "$ROOT/etc/passwd"
sed -i "s|^$OLD:|$NEW:|" "$ROOT/etc/shadow" 2>/dev/null || true
sed -i "s|^$OLD:|$NEW:|" "$ROOT/etc/gshadow" 2>/dev/null || true
sed -i "s|^$OLD:|$NEW:|" "$ROOT/etc/group" 2>/dev/null || true
sed -i -E "s/(^|[:,])$OLD([,:]|$)/\1$NEW\2/g" "$ROOT/etc/group" "$ROOT/etc/gshadow" 2>/dev/null || true

if [ -d "$ROOT/home/$OLD" ] && [ ! -d "$ROOT$NEWHOME" ]; then
    mv "$ROOT/home/$OLD" "$ROOT$NEWHOME"
fi
mkdir -p "$ROOT$NEWHOME" "$ROOT/home"
# 把旧 uid/gid 的文件统一到 1000
find "$ROOT" -xdev \( -uid "$OLD_UID" -o -gid "$OLD_GID" \) \
    -exec chown 1000:1000 {} + 2>/dev/null || true
chown -R 1000:1000 "$ROOT$NEWHOME" 2>/dev/null || true
# 删除 passwd/group 的 "-" 备份（里面还是旧 uid/用户名）
rm -f "$ROOT"/etc/passwd- "$ROOT"/etc/shadow- "$ROOT"/etc/group- "$ROOT"/etc/gshadow- 2>/dev/null || true

echo "== 4) 替换文本引用（/home/j716f、独立用户名 player、旧 uid 的 runtime 路径） =="
for f in "$ROOT"/etc/sudoers "$ROOT"/etc/sudoers.d/*; do
    [ -f "$f" ] || continue
    sed -i -E "s/\b$OLD\b/$NEW/g" "$f" 2>/dev/null || true
done

CHANGED=0
while IFS= read -r -d '' f; do
    case "$f" in
        *.bak-rename|*/.git/*|*/__pycache__/*|*/etc/ld.so.cache|*/etc/passwd|*/etc/group|*/etc/shadow|*/etc/gshadow) continue ;;
    esac
    grep -Iq . "$f" 2>/dev/null || continue
    if grep -qE "/home/$OLD|(^|[^A-Za-z0-9_])$OLD([^A-Za-z0-9_]|$)|/run/user/$OLD_UID" "$f" 2>/dev/null; then
        sed -i -E "s|/home/$OLD|$NEWHOME|g; s|/run/user/$OLD_UID|/run/user/1000|g; s/\b$OLD\b/$NEW/g" "$f"
        CHANGED=$((CHANGED+1))
        echo "  ~ ${f#$ROOT}"
    fi
done < <(find "$ROOT/etc" "$ROOT/usr/local" "$ROOT$NEWHOME" "$ROOT/var/lib/j716f" \
              -type f -print0 2>/dev/null)

echo "  共修改 $CHANGED 个文件"
echo "== 完成：$NEW  uid=1000 home=$NEWHOME =="
grep -E "^(player|player|alarm):" "$ROOT/etc/passwd" || true
