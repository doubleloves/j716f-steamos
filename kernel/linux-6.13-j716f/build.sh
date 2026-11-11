#!/bin/bash
# 下载上游 Linux v6.13，应用 J716F 补丁并编译 Image/dtbs/modules。
# 用法: ./build.sh [工作目录，默认 ./build]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:-$HERE/build}
VER=6.13
TARBALL=linux-$VER.tar.xz
URL=https://cdn.kernel.org/pub/linux/kernel/v6.x/$TARBALL

mkdir -p "$WORK"
cd "$WORK"

if [ ! -f "$TARBALL" ]; then
    echo "== 下载 $URL =="
    curl -L -o "$TARBALL" "$URL"
fi
if [ ! -d "linux-$VER" ]; then
    echo "== 解压 =="
    tar xf "$TARBALL"
fi

cd "linux-$VER"
if [ ! -f .config ]; then
    cp "$HERE/config-6.13-j716f" .config
fi
if [ ! -f .j716f-patched ]; then
    echo "== 应用补丁 =="
    patch -p1 < "$HERE/patches/0001-j716f-6.13.patch"
    touch .j716f-patched
fi

echo "== 编译 =="
make -j"$(nproc 2>/dev/null || echo 8)" Image dtbs modules

echo ""
echo "产物:"
echo "  Image: $(pwd)/arch/arm64/boot/Image"
echo "  DTB:   $(pwd)/arch/arm64/boot/dts/qcom/sm8250-lenovo-j716f.dtb"
