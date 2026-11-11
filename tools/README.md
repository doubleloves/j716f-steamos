# tools/ — 辅助工具

这些工具在 PC 或平板上运行，用于：分析 GPT 槽位、写 BCB、打包卡刷、构建 recovery、处理 boot.img、
生成文档图片等。大部分是 Python 3（`--help` 查看参数）。

## 槽位 / 启动

| 工具 | 运行位置 | 作用 |
|---|---|---|
| `gpt_slot_attrs.py` | PC/平板 | **只读**解析 GPT 分区属性，解码 A/B 的 active/success/retry 位；支持 dump 目录 diff |
| `slot-attr-dump.sh` | 平板 | 只读快照所有 LUN 的 GPT 头 + 属性 + misc 头，用于逆向/对比 |
| `write_bcb.py` | 平板 | 写 `misc` BCB `command`（`boot-recovery` 等），自动备份 4 KiB |
| `install-qbootctl.sh` | 平板 | 构建/安装 qbootctl，可选安装开机标记服务 |
| `j716f-mark-success.service` | 平板 | 每启动 `qbootctl -m`（解决 ABL 计数） |
| `build_boot_stable.py` | PC | 历史：从模板 boot.img 改 cmdline（加 mask）的示例 |
| `repack_boot_dtb.py` / `parse_boot_dtb.py` | PC | 替换/解析 boot.img 里的 DTB 与 header |
| `build_recovery_boot.py` | PC | 历史：修改 initramfs init 生成"救援 boot" |

## 卡刷 / recovery

| 工具 | 运行位置 | 作用 |
|---|---|---|
| `make-cardflash.py` | PC/平板 | 生成 `/j716f` payload + TWRP 兼容 zip（含 SHA256SUMS/manifest） |
| `make-mini-recovery.sh` | 平板 | 一键构建 mini-recovery（可选 `--flash` 写入 recovery_a/b） |
| `install-mini-recovery.sh` | 平板 | 把 boot.img 写入 recovery_a/b，自动备份原分区 |
| `reboot-to-recovery.sh` | 平板 | 系统内一键写 BCB 并重启进 recovery |
| `extract_ramdisk.py` / `init_patch.py` | PC | 解包/修改 initramfs 的 init（历史工具） |
| `backup_rootfs.py` | 平板 | 全量 rootfs 备份（tar） |
| `snapshot_partitions.py` | 平板 | 分区快照（调试/取证） |
| `mktar_kernel.py` | PC | 打包内核源码 tar（GPL 发布用） |

## 文档 / 仓库

| 工具 | 作用 |
|---|---|
| `make-doc-images.py` | 生成截图（二维码/头像打码）+ 启动链/发布流程/分区/槽位等示意图 |
| `sanitize-repo.py` | 仓库发布前脱敏（用户名、IP、序列号、MAC、旧家目录） |

## 使用示例

```bash
# 1) 看当前槽位状态（只读）
python3 tools/gpt_slot_attrs.py /dev/sde   # 平板上直接读
python3 tools/gpt_slot_attrs.py backup/partitions/sde-gpt-head.bin

# 2) 写 BCB 进 recovery
sudo python3 tools/write_bcb.py --command boot-recovery

# 3) 生成卡刷包
python3 tools/make-cardflash.py --boot boot.img --rootfs rootfs.img.gz \
  --out /mnt/sdcard --version v1.0.0 \
  --zip /mnt/sdcard/j716f-update.zip \
  --update-binary tablet/cardflash/update-binary  # 路径按实际调整

# 4) 重建 mini-recovery（平板）
sudo tools/make-mini-recovery.sh --flash

# 5) 生成文档图片
python3 tools/make-doc-images.py
```

> 注意：涉及写 GPT/BCB/分区的工具都有风险，先在测试机/备份后操作；`gpt_slot_attrs.py` 只读，可放心用。
