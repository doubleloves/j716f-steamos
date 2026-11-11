#!/bin/bash
# 生成完整 Linux 6.13 内核树（上游 + J716F 补丁 + config）到 tree/。
#
# 默认 tree/ 被 .gitignore 忽略；如果你确实想把完整内核树提交进本仓库：
#   1) 确认体积可接受（解压 ~1.6 GB，git 对象数百 MB）
#   2) 删除 .gitignore 里的 "kernel/*/tree/"
#   3) git add -f kernel/linux-6.13-j716f/tree
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
UPSTREAM_URL=https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.13.tar.xz
UPSTREAM_SHA=e79dcc6eb86695c6babfb07c2861912b635d5075c6cd1cd0567d1ea155f80d6e
WORK="$HERE/tree"

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f linux-6.13.tar.xz ]; then
    echo "== 下载上游 v6.13 =="
    curl -L -o linux-6.13.tar.xz "$UPSTREAM_URL"
fi
echo "$UPSTREAM_SHA  linux-6.13.tar.xz" | sha256sum -c -

if [ ! -d linux-6.13 ]; then
    tar xf linux-6.13.tar.xz
fi
cd linux-6.13
if [ ! -f .j716f-patched ]; then
    patch -p1 < "$HERE/patches/0001-j716f-6.13.patch"
    cp "$HERE/config-6.13-j716f" .config
    touch .j716f-patched
fi
echo "完整内核树: $WORK/linux-6.13"
du -sh "$WORK/linux-6.13"
