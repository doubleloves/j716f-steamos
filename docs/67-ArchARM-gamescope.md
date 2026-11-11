# 67 · Arch Linux ARM + gamescope（SteamOS 风格）实施方案

日期：2026-11-06
目标设备：Lenovo Xiaoxin Pad Pro 2021（TB-J716F，SM8250，aarch64）
当前系统：Ubuntu 24.04.5 LTS（内核 6.13.0-sm8250 定制树）
方案性质：**重装系统级工程**（不是装个软件）

---

## 0. 摘要（先看这段）

**可行，但代价明确**：

| 项目 | 评估 |
|---|---|
| 工程量 | 3–6 个工作日（含调试） |
| 最大风险 | 内核/固件/UCM/相机/旋转/温控等**全部设备定制要在 Arch 上重做或移植** |
| 收益 | gamescope 的 Game Mode 体验（帧率限制、FSR、direct scanout、更低延迟）+ 更接近 SteamOS 的会话架构 |
| 不收益 | **原始 FPS 不会因为换 Arch 而提高**（瓶颈在 FEX 转译、内存带宽、GPU 调频） |
| 可回滚性 | 好（保留 Ubuntu 分区 + boot_a 镜像 + EDL 恢复） |

**建议采用"双系统 + 分阶段"的方式做**：Ubuntu 留着当保险，Arch 装在新分区/外部介质，boot_a 与 boot_b 分别放两套内核。

---

## 1. 目标与验收标准

### 1.1 最终形态

```
开机 → ABL → boot_b（Arch 内核 + DTB + initramfs）
     → systemd → greetd → gamescope-session
     → gamescope (DRM 直出) → Steam Big Picture（ARM64 原生客户端）
     → 退出 Steam → 回到 greetd，可选择「桌面模式」
```

### 1.2 验收标准

| # | 项目 | 标准 |
|---|---|---|
| 1 | 启动 | 冷启动 ≤ 30 秒进入 Steam Big Picture |
| 2 | 显示 | 2560×1600 原生分辨率，无撕裂 |
| 3 | 触摸 | 触摸屏可用（gamescope 内 + 桌面） |
| 4 | 游戏 | Steam 能登录、能启动至少 1 个游戏（x86 经 FEX） |
| 5 | 音频 | 扬声器出声（CS35L41 四单元） |
| 6 | 网络 | WiFi 正常，蓝牙可用 |
| 7 | 桌面 | 可切回桌面模式（XFCE 或 labwc） |
| 8 | 设备 | 键盘/触摸板/旋转/充电温控 与当前持平 |
| 9 | 性能 | GPU 调频 305–670MHz 生效；zram 启用 |

---

## 2. 方案架构

### 2.1 启动链

| 环节 | 实现 |
|---|---|
| 引导 | ABL（高通引导器，已有 A/B 槽：`boot_a` / `boot_b`） |
| boot 镜像 | Android boot.img 格式：**kernel + ramdisk + DTB** |
| 打包工具 | 现有 `/home/j716f/tools/repack_boot.py`（支持 `--kernel/--ramdisk/--dtb`） |
| ramdisk | Arch 的 `mkinitcpio` 生成 initramfs（需含 ufs/ext4/msm 等） |
| cmdline | 需改为 Arch 根分区（`root=PARTUUID=...`） |

> 关键点：现有 flash 脚本只替换 kernel + DTB，ramdisk 沿用模板里的厂商 ramdisk。
> Arch 必须用自己的 initramfs → 需要给 flash 脚本加 `--ramdisk` 参数。

### 2.2 分区方案（三选一，见 §7 决策点）

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A. 双系统（推荐）** | 把 `sda18`（108GB ext4，现 Ubuntu）缩小到 ~60GB，新建 `sda19`（~45GB）装 Arch；`boot_a`=Ubuntu，`boot_b`=Arch | 保留 Ubuntu 随时回退；不用外部介质 | 需要缩分区（有风险，必须先备份）；切换槽位要验证 ABL 支持 |
| B. 全新安装 | 格式化 `sda18`，只装 Arch | 干净、磁盘规划简单 | Ubuntu 全丢，必须完整备份；调试期没系统可用 |
| C. microSD | 装到 SD 卡（`sdhc_2` 已启用，`cd-gpios`=tlmm 77） | 完全不碰内置存储 | **ABL 是否支持从 SD 启动未知**，需先实验；SD 卡速度慢，影响游戏加载 |

### 2.3 会话设计

```
greetd
├── gamescope-session（默认）
│     └── gamescope --backend drm -W 2560 -H 1600 -r 60 -f
│           └── steam -gamepadui -steamos3
└── desktop-session（可选）
      └── xfce4（X11）或 labwc（Wayland）
```

- **gamescope 包**：Arch Linux ARM 官方仓库有 aarch64 版（`archlinuxarm.org/packages/aarch64/gamescope`），不用自己编译
- **桌面模式**：建议 **labwc**（Wayland，轻量 ~50MB）替代 XFCE，省内存；但旋转/输入法要重新适配
- **旋转**：gamescope 内锁横屏即可；桌面模式若要自动旋转，Wayland 下用 `wlr-randr`/`kanshi`，X11 下沿用现有守护

### 2.4 软件栈

| 组件 | 来源 |
|---|---|
| 内核 | 自建：`/home/j716f/j716f-linux`（6.13 树）+ 现有 `.config`，打成 Arch 包 `linux-j716f` |
| 固件 | 从当前系统复制 `/lib/firmware/qcom/sm8250/`（ADSP/CDSP/SLPI/Adreno/WCN6855） |
| 图形 | Mesa（Freedreno + Turnip），Arch 仓库自带，版本比 Ubuntu 新 |
| 音频 UCM | 复制 `/usr/share/alsa/ucm2/conf.d/sm8250/Lenovo Xiaoxin Pad Pro 2021.conf` |
| Steam | Steam-ARM（Valve ARM64 客户端）+ FEX + ARM64 Proton |
| FEX | 源码编译或 Steam-ARM 自带 rootfs |

---

## 3. 现状盘点（必须保留的能力）

| 功能 | 当前实现 | 迁移难度 | 备注 |
|---|---|---|---|
| 显示（双 DSI panel） | 内核树内 DPU/DSI 补丁 + panel 时序 | ★★★ | 必须用同一内核树，不能换 Arch 官方内核 |
| 触摸屏 | goodix_ts | ★ | 内核驱动，随内核走 |
| 原装键盘 | i2c-hid（i2c-4 @0x61） | ★ | 随内核 |
| 键盘保护套触摸板 | 04F3:3164（i2c-hid） | ★ | 随内核 |
| WiFi/BT | WCN6855 + 固件 + board files | ★★ | 固件路径要对 |
| 扬声器 | CS35L41 ASoC + UCM 配置 | ★★ | UCM 文件要部署到 Arch 对应路径 |
| 相机（多摄） | 内核驱动 + `j716f-camera-*.service` + 脚本 | ★★★ | daemon/脚本要重新打包 |
| 自动旋转 | `j716f-rotate.py` + `.xinitrc` | ★★ | Wayland 下需换方案 |
| 快充温控 | `j716f-charge-thermal.service` + python | ★ | 直接搬 |
| idle-fix / pstore-tune / stability | systemd 服务 | ★ | 直接搬 |
| 桌面 | XFCE + fcitx5 | ★★ | 建议换 labwc |
| 网络管理 | NetworkManager + SSH | ★ | 标准包 |
| 内核打包 | 现有 `flash-kernel.sh`（dd 写 boot_a/b） | ★★ | 需要加 ramdisk 参数 |

---

## 4. 待解决问题清单

### P0 — 不解决就起不来

| # | 问题 | 方案草案 | 风险 |
|---|---|---|---|
| 1 | **安装目标与分区** | 见 §2.2；推荐缩 sda18 + 新建 sda19 | 缩分区有数据风险，必须先全量备份 |
| 2 | **内核打包** | 用现有树 + `.config` 编 `Image` + modules；写 PKGBUILD 生成 `linux-j716f` 包（含 `/boot/Image`、`/usr/lib/modules/6.13.0-sm8250`、`/usr/lib/firmware`） | 模块依赖/固件路径要与 Arch 一致 |
| 3 | **initramfs** | `mkinitcpio` 配置：MODULES=(ufs phy-qcom-qmp phy-qcom-ufs-qmp-v4 ext4 msm panel-edp?)；HOOKS=(base udev modconf block filesystems) | 漏模块会导致启动卡住 |
| 4 | **boot.img 打包** | 给 `flash-kernel.sh`/`repack_boot.py` 增加 `--ramdisk` 与 `--cmdline` 支持；用 Arch initramfs + 正确 root= | cmdline 写错会 panic |
| 5 | **显示 bring-up** | 直接复用现有内核/DTB（已验证），先确认 Arch 下 KMS 正常（modetest / kmscube） | 低 |
| 6 | **输入** | 内核自带；确认 `/dev/input/event*`、libinput 识别 | 低 |
| 7 | **网络** | WCN6855 固件 + board-2.bin；先用 USB 网卡或 netconsole 兜底 | 中（调试期没网很难受） |

### P1 — 功能缺失

| # | 问题 | 方案草案 | 风险 |
|---|---|---|---|
| 8 | **扬声器** | 复制 UCM 文件到 `/usr/share/alsa/ucm2/conf.d/sm8250/`；确认 CS35L41 驱动模块加载 | 中（声道映射可能需调） |
| 9 | **相机** | 内核驱动随内核；打包 `j716f-camera-setup.sh` + 3 个 daemon 服务 + 依赖 | 高（脚本依赖 Ubuntu 路径/python 包） |
| 10 | **旋转** | gamescope 锁横屏；桌面模式 Wayland 用 `wlr-randr` 监听 `iio`？本机无 IIO，需用 `ssccli`/libssc 读传感器 | 中（现有守护是 X11 的） |
| 11 | **温控/充电** | 直接搬 `j716f-charge-thermal.py` + systemd 单元 | 低 |
| 12 | **电源管理** | suspend/resume 验证；GPU devfreq 305–670MHz 用 systemd 单元固化；zram 配置 | 中 |

### P2 — 体验

| # | 问题 | 方案草案 | 风险 |
|---|---|---|---|
| 13 | **gamescope 会话** | Arch ARM 官方包；greetd 配置 `gamescope-session`；Steam 启动参数 `-gamepadui -steamos3` | 中（DRM 后端 vs X11） |
| 14 | **Steam-ARM 在 Arch 上** | ⚠️ **最高风险项**：Steam-ARM 项目面向 Debian/Ubuntu，Arch 无官方包。方案：解包 .deb（`bsdtar`）到 `/opt/steam-arm`，补齐 Arch 等价依赖（sdl3、libxkbcommon、pulseaudio 等），用其启动脚本 | 高 |
| 15 | **FEX** | Arch ARM 无官方包；源码编译 FEX-Emu（aarch64 宿主），或复用 Steam-ARM 的 `/opt/fex-rootfs` | 中高 |
| 16 | **ARM64 Proton** | 随 Steam 客户端安装（Steam Linux Runtime）；需确认 Arch 下 runtime 可用 | 中 |
| 17 | **输入法/字体/locale** | 桌面模式才需要：fcitx5 + 中文字体 + locale | 低 |
| 18 | **蓝牙音频** | BlueZ + PipeWire/PulseAudio；与 Steam 语音并存 | 低 |
| 19 | **屏幕键盘** | Steam Big Picture 自带 OSK，触屏可用 | 低 |

### 附：明确不解决的问题

- **内置麦克风**：已归档（ES7210 阵列被 DSP 的 MI2S TX 挡住；WCD DMIC/AMIC 无信号）→ 用 USB 麦克风
- **SteamOS 本体**：x86_64 专用，不可用

---

## 5. 分阶段实施计划

> 每个阶段都有验收标准和回滚点；未通过不进入下一阶段。

### Phase 0 · 备份与准备（0.5 天）

- [ ] 备份 `boot_a` / `boot_b` 镜像（已有 `/home/j716f/backup/`）
- [ ] 备份 Ubuntu 关键数据（`/home/j716f/docs`、`j716f-linux` 源码、工具、备份目录）到外部/网络
- [ ] 记录当前分区表（`sfdisk -d /dev/sda > sda.sfdisk`）
- [ ] 下载 Arch Linux ARM aarch64 rootfs tarball（验证 SHA256）
- [ ] 确认 EDL 恢复流程可用（模板镜像 `boot-template-dbmdx.img`）
- **验收**：数据有异地副本 + 恢复步骤演练过

### Phase 1 · 最小系统（0.5–1 天）

- [ ] 分区（缩 sda18 / 新建 sda19）与格式化（ext4 或 btrfs）
- [ ] 解压 rootfs，chroot 配置：locale、时区、root 密码、SSH、NetworkManager
- [ ] 编译内核 + 模块，安装到目标分区
- [ ] 生成 initramfs，打包 boot.img（含 ramdisk + cmdline），刷入 `boot_b`
- [ ] 用 `fastboot --set-active=b`（或现有 ABL 切换方式）引导 Arch
- **验收**：能进 Arch 控制台（HDMI 无、靠串口/netconsole 或屏幕），SSH 可用

### Phase 2 · 外设（0.5–1 天）

- [ ] 显示（KMS 正常，2560×1600）
- [ ] 触摸、键盘、触摸板（libinput 识别）
- [ ] WiFi + SSH 稳定；蓝牙
- **验收**：可正常操作 + 联网

### Phase 3 · 设备功能（1–2 天）

- [ ] 扬声器（UCM + CS35L41）
- [ ] 相机（驱动 + daemon 脚本移植）
- [ ] 旋转（桌面模式）
- [ ] 温控/充电/idle-fix/pstore-tune/stability 服务
- [ ] suspend/resume
- **验收**：功能与 Ubuntu 持平（麦克风除外）

### Phase 4 · 图形 + gamescope（0.5–1 天）

- [ ] Mesa/Turnip/Vulkan 验证（`vulkaninfo`、`kmscube`）
- [ ] 安装 gamescope（Arch ARM 包）
- [ ] greetd + gamescope-session 配置，Steam 先用占位程序验证会话能起
- [ ] GPU 调频 305–670MHz 固化
- **验收**：gamescope 能全屏出画面

### Phase 5 · Steam + 游戏（0.5–1 天）

- [ ] 安装 Steam-ARM（解包 .deb + 依赖）
- [ ] FEX 配置（binfmt / FEXBash）
- [ ] ARM64 Proton 安装
- [ ] Big Picture 登录、下载并运行 1 个游戏
- **验收**：游戏可玩；这是**整个项目成败的关键节点**——若不通过，回滚 Ubuntu

### Phase 6 · 调优（0.5 天）

- [ ] zram 6GB、swappiness
- [ ] 内存优化（关 blueman/xfdesktop 等）
- [ ] 帧率限制/FSR 预设
- [ ] 实测游戏帧数、温度、续航
- **验收**：达到 §1.2 全部标准

---

## 6. 风险登记表

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| Steam-ARM 在 Arch 上跑不起来 | **高** | 项目失败 | Phase 5 前先做最小验证（解包 + 启动器 + 登录），失败则回滚 Ubuntu |
| 缩分区失败导致数据丢失 | 中 | 高 | 全量备份 + `sfdisk` 导出 + 先演练 |
| ABL 不支持切换槽位 | 中 | 双系统方案失效 | 先验证 `fastboot set-active`；否则退回单系统 |
| 内核/固件在 Arch 下不工作 | 中 | 高 | 用同一内核树 + 同一 .config，先跑通串口 |
| 相机 daemon 依赖 Ubuntu 环境 | 中 | 中 | 可延后，不影响游戏 |
| gamescope DRM 后端在 msm 上异常 | 中 | 中 | 备选：gamescope X11 嵌套 + Xorg |
| 时间超预期 | 高 | 中 | 分阶段交付，每阶段可停在可用状态 |

---

## 7. 需要你确认的决策点

| # | 决策 | 选项 | 建议 |
|---|---|---|---|
| 1 | **装到哪里** | A 双系统（缩 sda18）/ B 全新 / C microSD | **A**（保留 Ubuntu） |
| 2 | **桌面模式** | 保留 XFCE / 换 labwc / 不要桌面 | labwc（省内存，但要适配旋转） |
| 3 | **中文输入法** | 需要 / 不需要 | 桌面模式再装 |
| 4 | **时间窗口** | 是否接受 1–2 天不可用（Phase 1 调试期） | 建议选周末 |
| 5 | **外部存储** | 有没有 U 盘/移动硬盘用于备份 | 没有的话先网络备份 |

---

## 8. 参考资源

- gamescope（Arch Linux ARM aarch64 包）：`archlinuxarm.org/packages/aarch64/gamescope`
- Steam-ARM 项目：`github.com/Scrumpper/Steam-ARM`（现有 Ubuntu 成功记录见 `docs/18`）
- 本机内核树：`/home/j716f/j716f-linux`（6.13.0-sm8250）
- 现有 boot 打包：`/home/j716f/tools/repack_boot.py`、`/home/j716f/flash-kernel.sh`
- 设备功能记录：`docs/56`（音频/扬声器）、`docs/60`（交接）、`docs/61`–`docs/66`（音频调查）
- FEX-Emu：`github.com/FEX-Emu/FEX`

---

## 9. 与"Ubuntu + Steam-ARM"方案的对比结论

| 维度 | Arch + gamescope | Ubuntu + Steam-ARM |
|---|---|---|
| Game Mode 体验 | ✅ 完整（gamescope） | ⚠️ X11 Big Picture |
| 原始 FPS | 相同（瓶颈不在发行版） | 相同 |
| 内存 | 低 ~400–500MB（不用完整桌面） | 可优化到接近 |
| 设备适配 | ❌ 全部重做 | ✅ 已就绪 |
| 风险 | 高 | 低 |
| 时间 | 3–6 天 | 1–2 小时 |

**结论**：Arch + gamescope 是"体验升级 + 工程重做"的组合。既然决定做，就按 §5 分阶段推进，**Phase 5（Steam 能跑起来）是止损点**：如果 Steam-ARM 在 Arch 上无法工作，立即回滚到 Ubuntu 方案（同样的 gamescope 可以在 Ubuntu 上补）。
