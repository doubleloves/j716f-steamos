#!/bin/bash
# 从运行中的系统重启进入 mini-recovery（发布版入口，不需要按键/电脑）。
exec python3 /usr/local/sbin/j716f-write-bcb.py --command boot-recovery
