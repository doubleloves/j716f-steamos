#!/bin/bash
# 在设备/PC 上生成 6.13 设备补丁集：
#   vanilla/linux-6.13   <- 上游 https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.13.tar.xz
#   src/                 <- 只包含"改动过的"源文件（保留相对路径）
# 输出：j716f-6.13.patch，并用 patch -p1 --dry-run 验证。
set -euo pipefail

W=${1:-$PWD}
V="$W/vanilla/linux-6.13"
S="$W/src"
OUT="$W/j716f-6.13.patch"

[ -d "$V" ] || { echo "缺少 $V"; exit 1; }
[ -d "$S" ] || { echo "缺少 $S"; exit 1; }

: > "$OUT"
n_changed=0
n_new=0
while IFS= read -r -d '' f; do
    rel=${f#"$S"/}
    if [ -f "$V/$rel" ]; then
        if ! cmp -s "$V/$rel" "$S/$rel"; then
            diff -u --label "a/$rel" --label "b/$rel" "$V/$rel" "$S/$rel" >> "$OUT" || true
            n_changed=$((n_changed+1))
        fi
    else
        diff -uN --label "a/$rel" --label "b/$rel" /dev/null "$S/$rel" >> "$OUT" || true
        n_new=$((n_new+1))
    fi
done < <(find "$S" -type f -print0)

echo "patch: $OUT ($(wc -l < "$OUT") 行, 修改 $n_changed 个文件, 新增 $n_new 个文件)"

# 验证能干净应用到上游源码（dry-run，不修改 vanilla）
if ( cd "$V" && patch -p1 --dry-run < "$OUT" >/tmp/patch-dry.log 2>&1 ); then
    echo "APPLY_OK"
else
    echo "APPLY_FAIL"
    tail -20 /tmp/patch-dry.log
    exit 1
fi
