# recovery/ — mini-recovery（无 PC 卡刷）

一个极小的"恢复系统"：以 Android boot.img 形式写进空闲的 `recovery_a/recovery_b`（各 100 MiB），
通过 `misc` 的 BCB `boot-recovery` 从系统内重启进入，从 SD/USB 的 `/j716f/` 读取 payload 刷写。

## 文件

| 文件 | 作用 |
|---|---|
| `init` | initramfs 的 PID1：挂 proc/sys/dev、tty 横幅、90 秒看门狗、exec `/recovery.sh` |
| `recovery.sh` | 主逻辑：挂 userdata 写日志、启 USB gadget/telnet、扫描介质、校验、写 boot/rootfs、qbootctl、清 BCB、重启 |
| `build_mr.py` | 构建器：取一个可启动的 boot.img 为底，替换 initramfs 的 init、加入 recovery.sh/qbootctl/by-partlabel.map，重打包 |
| `extract_file.py` | 从 boot.img 的 initramfs 里取文件（例如取 busybox 做语法检查） |

## 构建

在平板上：

```bash
sudo /home/yp/tools/make-mini-recovery.sh            # 只构建 -> /home/yp/deploy/boot-minirecovery.img
sudo /home/yp/tools/make-mini-recovery.sh --flash    # 构建并写入 recovery_a/recovery_b
```

脚本内部：

1. `dd` 当前 `boot_a` 作为底（保证 kernel/DTB 与当前系统一致）；
2. 构建静态 `qbootctl`（glibc static，initramfs 里可运行）；
3. 生成 `/etc/by-partlabel.map`（initramfs 无 udev，qbootctl 需要 by-partlabel 目录）；
4. `build_mr.py` 替换 init 并加入文件，重打包成 31.7 MB 的 boot.img；
5. 可选：`install-mini-recovery.sh` 备份原 recovery 后写入 `recovery_a/b`。

## 进入 recovery

```bash
sudo /home/yp/tools/reboot-to-recovery.sh
# 等价于
sudo python3 /home/yp/tools/write_bcb.py --command boot-recovery
```

- `write_bcb.py` 只写 `misc` 前 32 B 的 `command` 字段，并把原 4 KiB 备份到 `/root/`；
- ABL 下次启动引导 `recovery_a/b`；recovery 启动后**尽早清掉 BCB**，不会恢复循环；
- 已知限制：按键组合（音量加+电源）未确认；没有 BCB 时可用 PC `fastboot reboot recovery`。

## payload 布局（SD/USB 根目录）

```
/j716f/SHA256SUMS      # 必须；校验不通过不写任何分区
/j716f/boot.img        # 可选：写入 boot_a + boot_b
/j716f/rootfs.img.gz   # 可选：整盘写入 userdata（会清空数据）
/j716f/HOLD            # 可选：存在则不自动重启，停在 shell（telnet/串口可用）
```

- 支持 FAT32 和 ext4 介质；内部 UFS 的 `sd[a-f]*` 会被跳过，只扫 SD（`mmcblk*`）和 USB（`sd[g-z]*`）；
- 校验用 busybox 手写循环（兼容旧 busybox，不用 `sha256sum -c`）；
- 写完后 `qbootctl -m` 标记槽位成功。

## 日志 / 观察

- 日志：`/recovery.log`（userdata 根目录，重启后可直接查看）；
- 控制台：`/dev/tty0`、`/dev/tty1`；无屏幕时也能通过日志判断；
- USB gadget：RNDIS `172.16.42.1/24` + telnetd（`telnet 172.16.42.1`）。

## 安全设计

| 机制 | 说明 |
|---|---|
| SHA256 校验 | 只有校验通过才写分区 |
| 写前检查 | boot.img 必须 `ANDROID!` magic；rootfs 需显式提供才会覆盖 |
| BCB 清理 | 启动后立即清 `misc` 命令，避免反复进 recovery |
| 90 秒看门狗 | init 后台 `sleep 90; reboot -f`，异常不会卡死 |
| 自动重启 | 无 payload 时 20 秒后重启回系统 |
| 不碰 vendor 分区 | 只写 `boot_a/b`、`userdata`、清 BCB |

## 测试状态

- mini-recovery 启动、BCB 进入、无 payload 自动回退：✅ 实机验证
- SD 卡 payload 刷 boot_a/b + qbootctl 标记 + 回系统：✅ 实机验证
- rootfs 整盘写入：⏳ 流程就绪，未做清数据验证
- 屏幕 UI：⏳ 目前以日志/自动流程为主，未验证显示画面
