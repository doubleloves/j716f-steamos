#!/bin/bash
# 从运行中的系统重启进入 mini-recovery（不需按键/电脑）。
#
# 做法：往 misc 的 bootloader_message.command 写入 "boot-recovery"，
# 下次 ABL 启动会引导 recovery_a/b 里的 mini-recovery。
# recovery 跑完会自己清掉这个 command 并重启回系统，不会循环。
set -e

[ "$(id -u)" -eq 0 ] || { echo "请用 root 运行"; exit 1; }
[ -x /usr/local/sbin/qbootctl ] || echo "警告: qbootctl 未安装，recovery 内无法标记槽位（不影响 boot 刷写）"

exec python3 /home/j716f/tools/write_bcb.py --command boot-recovery
