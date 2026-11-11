# CHANGELOG

## 2026-11-11（文档/内核）

- 新增英文 README（`README.en.md`）
- 新增 `docs/screenshots.md`：实机 Steam Big Picture 截图（二维码/头像已打码）+ 启动链/发布流程/分区/槽位/卡刷流程示意图
- 新增 `tools/make-doc-images.py`（截图打码 + 示意图生成）
- 新增详细文档：`docs/87`（仓库导读）、`docs/88`（内核与启动镜像）、`docs/89`（FAQ/排障）、`docs/90`（性能与兼容性）
- 新增目录说明：`scripts/`、`configs/`、`release/`、`recovery/`、`tools/` 的 `README.md`；新增 `CONTRIBUTING.md`
- 内核目录重构：
  - `kernel/linux-6.13-j716f/`：完整设备补丁集（36 修改 + 42 新增，相对上游 v6.13）、实机 `.config`、
    **全部 80 个改动/新增源文件（`src/`，约 2.5 MB）**、DTB、`build.sh`、`import-full-tree.sh`、`UPSTREAM.md`
  - `kernel/linux-6.15.11-j716f-ubuntu/`：历史 6.15.11 补丁/DSI/麦克风快照
  - `kernel/README.md` 说明 patch+config+src 策略与"完整树入库"的可选方案（体积/GitHub 限制）

## 2026-11-11

- 发布版构建工具链：`release/build-rootfs.sh`（rsync 当前系统 → 20G ext4 镜像 → 清理 → 首启 → gz/simg）
- 发布版用户策略：`yp` → 占位 `player`，家目录固定 `/home/j716f`；首启 setup 自定义登录名/密码（`docs/86`）
- 发布版不含 Valve 客户端：首启 setup console + `j716f-install-steam.sh`（用户设备自行下载）+ 可选离线包
- 实测通过：mini-recovery 写入 `recovery_a/b`、BCB `boot-recovery` 进入、SD 卡 payload 刷 boot、自动回系统
- 实测通过：GPT A/B 属性位 + `qbootctl -m` 每启动标记成功，ABL 重试计数不再递减（`docs/83`）

## 2026-11-10

- 修复 ABL 槽位重试计数（"重启约 7 次进 fastboot"）：定位到 GPT 分区属性 byte54，bits 3–5 = retry、
  bit 6 = successful；用 qbootctl 每启动 `-m` 解决，连续重启验证通过
- 构建 mini-recovery（kernel + 新 initramfs + static qbootctl + by-partlabel 映射），写入 `recovery_a/b`
- 卡刷工具：`make-cardflash.py`（`/j716f` payload + TWRP 兼容 zip）、`write_bcb.py`（进入 recovery）
- 发布/刷机文档 82–84

## 2026-11-08 ～ 2026-11-09

- 稳定基线（内核 6.13.0-sm8250、gamescope 3.17、Steam-ARM、BlueZ 5.72、zram、CPU/GPU 调频）
- CPU 3.2GHz / GPU 670MHz 实验与热控调研（默认不启用）
- 蓝牙、快充、麦克风、相机等设备问题记录（`docs/device/`、`docs/72/73/78`）

## 2026-11-06

- 从 Ubuntu 迁移到 Arch Linux ARM + gamescope + greetd（`docs/71` 完整实施记录）
- 兼容层：FEX-Emu + ARM64 Proton；x86 游戏可用性验证

## 2026-10-26

- 在 Ubuntu 上首次跑通 Valve 原生 ARM64 Steam 客户端 + Big Picture（`docs/ref-18.md`，历史记录）
