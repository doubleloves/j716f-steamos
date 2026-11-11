# Kernel 6.13 (J716F) — 设备补丁集 + 改动源码 + 构建

本目录对应设备实机运行的 **Linux 6.13.0-sm8250**（Arch Linux ARM aarch64）。
为了既能在 GitHub 上直接浏览设备相关代码、又不把 1.6 GB 的完整内核树塞进仓库，这里采用：

- **完整上游可复现**：上游 tarball 地址 + SHA-256 + 补丁 + `.config`；
- **设备改动源码可直接浏览**：`src/` 里放了全部 80 个被修改/新增的源文件（约 2.5 MB）；
- **一条命令编译**：`build.sh` 自动下载上游、打补丁、编译；
- **可选完整树**：`import-full-tree.sh` 生成 `tree/`（默认 gitignore；如需入库见文末）。

## 目录结构

```
linux-6.13-j716f/
├─ UPSTREAM.md                  # 上游地址/SHA256、校验与复现命令
├─ patches/0001-j716f-6.13.patch# 相对 v6.13 的全部改动（36 修改 + 42 新增，1.1 MB）
├─ patches/series
├─ config-6.13-j716f           # 实机 .config（232 KB）
├─ src/                        # 80 个改动/新增源文件（按内核树相对路径，2.5 MB）
├─ dts/                        # 设备树单文件副本（方便 diff）
├─ candidate-files.txt         # 生成补丁时的源文件清单（审计用）
├─ make-patch.sh               # 从"改动文件 + 上游源码"重新生成 patch
├─ import-full-tree.sh         # 生成完整内核树到 tree/（可选）
└─ build.sh                    # 下载 → 打补丁 → 编译 Image/dtbs/modules
```

## 快速编译

```bash
cd kernel/linux-6.13-j716f
./build.sh
# 产物：
#   build/linux-6.13/arch/arm64/boot/Image
#   build/linux-6.13/arch/arm64/boot/dts/qcom/sm8250-lenovo-j716f.dtb
```

依赖：`gcc-aarch64-linux-gnu`（或本机 aarch64 原生编译）、`make`、`bison`、`flex`、`bc`、`libssl-dev`、
`libelf-dev`。在平板本机编译时直接用 `make -j8 Image dtbs modules` 即可（不需要交叉工具链）。

编译完成后把 `Image` + initramfs + DTB 打成 Android boot.img（见 `tools/`、`docs/67`、`docs/71`），
写入 `boot_a`/`boot_b`，或通过发布包的卡刷/线刷更新。

## 补丁内容概览（按子系统）

| 子系统 | 主要文件 | 说明 |
|---|---|---|
| 设备树 | `arch/arm64/boot/dts/qcom/sm8250-lenovo-j716f*.dts`、`sm8250.dtsi`、`pm8150b.dtsi` | 面板/触摸/键盘/音频/充电/传感器等板级描述 |
| 显示 | `sm8250.dtsi` + DSI 补丁（另见 `kernel/linux-6.15.11-j716f-ubuntu/dsi*.patch`） | rm69380 AMOLED 双 DSI、DPU/DSI 时序 |
| 音频 | `sound/soc/codecs/{cs35l41-lib,wcd938x*,es7210,dbmdx}`, `sound/soc/qcom/*` | CS35L41 扬声器、WCD938x、ES7210 麦克风阵列、dbmdx DSP、q6afe/q6asm/q6routing/q6lsm |
| 相机 | `drivers/media/i2c/{ov13b10,gc5035,s5k4h7}`, `camss-*`, `venus/*` | 多摄 sensor + CAMSS/Venus |
| 充电/电量 | `drivers/power/supply/{qcom_pm8150b_charger,qcom_fg,power_supply_sysfs}` | SMB5 快充 + 电量计 |
| 蓝牙 | `drivers/bluetooth/hci_qca.c` | QCA6390 时序/电压修正 |
| 其它 | `soundwire/qcom.c`, `sound/core/pcm_native.c`, `soc-pcm.c` | SoundWire、低延迟/路由修正 |

> 补丁中的 `dbmdx`、`es7210`、`camss` 等含设备原厂/社区来源代码，原始许可与出处请自行核对；
> 对外分发前确认合规。

## 从自己的内核树重新生成补丁

```bash
# 工作目录结构：
#   work/vanilla/linux-6.13/   <- 上游源码
#   work/src/                  <- 改动过的文件（按相对路径）
./make-patch.sh /path/to/work

# 输出：work/j716f-6.13.patch，并自动 patch -p1 --dry-run 验证
```

`candidate-files.txt` 是本次生成时比对的文件清单（80 个），可作为审计/复核依据。

## 完整内核树（可选）

```bash
./import-full-tree.sh
# -> tree/linux-6.13（上游 + 补丁 + .config，约 1.6 GB）
```

`tree/` 默认被 `.gitignore` 忽略。如果确实要把完整树提交进本仓库：
1. 确认体积可接受（解压约 1.6 GB；git 对象数百 MB，已接近 GitHub 仓库体积建议上限）；
2. 从 `.gitignore` 去掉 `kernel/*/tree/`；
3. `git add -f kernel/linux-6.13-j716f/tree`。

## GPL 合规

Linux 内核为 GPL-2.0。发布包含内核二进制的镜像时，需要提供对应源码。本仓库提供：

- 上游 tarball 地址 + SHA-256（`UPSTREAM.md`）；
- 完整设备补丁（可精确复现）；
- 实机 `.config`；
- 可选的完整树生成脚本（`import-full-tree.sh`）。

这满足"可获取相应源码"的常见做法；如果发布 Release 镜像，建议在 Release 说明里同时贴出
上游地址 + 本目录链接，或附上 `tree/linux-6.13` 的源码压缩包（分卷 <100 MB/文件）。
