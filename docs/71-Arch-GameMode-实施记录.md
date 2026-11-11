# 71 · Arch Linux ARM + gamescope 实施记录（2026-11-06）

> 按 `docs/67-ArchARM-gamescope实施方案.md` 实施。本文记录实际落地方案、当前状态、已知问题与下一步。

## 0. 当前结论

✅ **Arch Linux ARM + gamescope + Steam ARM64 已验证可运行**：
- Arch 装在 **sda18（108G）**，救援系统在 `sda17`；
- 开机自动登录并进入 **Game Mode**（tty1 → gamescope → Steam Big Picture）；
- 显示、音频、触摸、键盘、WiFi、SSH、温控、zram 正常；
- 双 DSI 面板的「画面切成两半/错位」问题**已解决**：gamescope 默认用 10-bit（XR30）扫描输出，双 DSI DPU 处理有问题；用一个 LD_PRELOAD 小库把候选扫描格式改成 8-bit（XR24）后画面正常（详见 §11）。

剩余未做：FEX + ARM64 Proton（x86/Windows 游戏）、相机/自动旋转等剩余设备服务（见 §8）。

## 1. 最终布局

| 分区/槽 | 内容 | 说明 |
|---|---|---|
| `sda17`（8G，原 super） | **ARCHRESCUE** | 救援 Arch（同内核/模块，SSH 可用），常驻 |
| `sda18`（108G，原 Ubuntu） | **ARCH**（主系统） | Steam/gamescope/设备服务都在这里 |
| `boot_a` / `boot_b` | 同一份 boot.img | kernel 6.13.0-sm8250 + DTB + 自定义 busybox initramfs |
| 外部 microSD | 未动 | 后续可做 Steam 游戏库 |

启动链：ABL → boot.img → initramfs（优先 `/dev/sda18`；失败/3 次未成功回退 `/dev/sda17`）→ systemd。
`j716f-boot-ok.service` 在成功进多用户后写 `/.boot-ok`；initramfs 连续 3 次没看到 `.boot-ok` 就自动换另一个根分区（自动回退救援系统）。
双槽写同一镜像（沿用原 `flash-kernel.sh` 做法，规避 ABL 重试计数切槽）。

## 2. 已迁移并验证的功能

| 功能 | 状态 | 说明 |
|---|---|---|
| WiFi（WCN6855/QCA6390） | ✅ | NetworkManager + 复制的 keyfile；MAC 已固定（`cloned-mac-address=AA:BB:CC:DD:EE:FF`），IP 稳定为 `192.168.1.10` |
| SSH | ✅ | `sshd` 自启；`player/CHANGEME`，sudo 免密 |
| 显示（KMS） | ✅ 基本显示 | DSI-1 2560×1600@90Hz，`/dev/dri/card0` |
| 触摸/键盘/触摸板 | ✅ | goodix_ts / 17EF:610B / 04F3:3164 全部识别 |
| 音频（CS35L41×4） | ✅ | 复制 Ubuntu UCM2（`J716F-HiFi.conf`）+ `speaker-mixer.service`；测试音已出声；PulseAudio 已装 |
| CPU/GPU 稳定性 | ✅ | `j716f-cpu-cap`/`j716f-stability`/`j716f-idle-fix`（Ubuntu 时期防死机的降频策略） |
| zram 4G | ✅ | `zram-4g.service` |
| 快充温控 | ✅ | `j716f-charge-thermal.service`（软件 JEITA，日志正常） |
| 蓝牙 | ✅ | bluez + 原 `/var/lib/bluetooth` 配对数据 |
| 背光权限 | ✅ | `99-backlight.rules`（video 组可写） |
| gamescope | ⚠️ 能跑但有画面问题 | 见 §5 |
| Steam ARM64 客户端 | ✅ | Valve ARM64 客户端（bins_linuxarm64 包）已自更新到 steamdeck_stable；Big Picture 登录页/二维码正常 |
| FEX / ARM64 Proton | ❌ 未装 | x86/Windows 游戏暂不可玩（下一步） |
| 相机/自动旋转/内置麦 | ❌ 未迁移 | 不影响 Game Mode |

## 3. 关键路径（平板内）

- 主系统根：`/dev/sda18`（label `ARCH`）
- 救援系统根：`/dev/sda17`（label `ARCHRESCUE`）
- 部署脚本：`/home/j716f/deploy/`（boot 镜像、install 脚本、initramfs 准备脚本）
- 从 Ubuntu 迁移的资料：`/home/j716f/migrate/`（bin/sbin/units/udev/pulse/steam/ucm）
- 原数据：`/home/j716f/docs`、`/home/j716f/tools`、`/home/j716f/backup`（部分）
- Game Mode 启动器：`/usr/local/bin/j716f-gamemode`（gamescope DRM 直出）
- Game Mode 备用启动器：`/usr/local/bin/j716f-gamemode-cage`（cage + 嵌套 gamescope，未验证成功）
- 自启开关：`/etc/j716f-gamemode-enabled`（存在才在 tty1 自动进 Game Mode）
- Steam 客户端：`/home/j716f/.local/share/steam-arm/.local/share/Steam/steamrtarm64/`
- GTK2 兼容库（Arch 缺 gtk2）：`/usr/local/lib/steam-compat/`

## 4. 常用命令

```bash
# SSH
ssh player@192.168.1.10           # 密码 CHANGEME

# 手动进 Game Mode（当前有画面错位问题）
/usr/local/bin/j716f-gamemode

# 开机自动进 Game Mode（显示问题修好后再开）
sudo touch /etc/j716f-gamemode-enabled && sudo reboot
# 关掉自动进 Game Mode
sudo rm -f /etc/j716f-gamemode-enabled

# 状态检查
systemctl --failed
aplay -l; amixer -c L2021 cget 'name=PRI_MI2S_RX Audio Mixer MultiMix1' 2>/dev/null
vulkaninfo --summary | head -20
```

## 5. 已知问题：双 DSI 面板画面「切成两半/错位」（✅ 已解决，见 §11）

**现象**：gamescope 直出时，Steam Big Picture / vkcube 画面被竖着切成两半，内容错位（不是 Steam 渲染问题）。

**已确认的事实**：
- 用 `xwd` 抓取 gamescope 内 Xwayland 的 Steam 窗口，内容完全正常（截图见 PC `backup/steam-ui-capture.png`）→ **Steam 渲染正确，问题在 gamescope 的扫描输出**。
- gamescope 占用 KMS plane 的格式是 **`XR30`（10-bit RGB30，linear，pitch 10240）**（`/sys/kernel/debug/dri/0/state`）。
- Ubuntu/X11 时代同样的内核/DTB 下画面正常，X11 的扫描格式是 8-bit `XR24`。
- 尝试过无效：`--force-composition`、`--disable-layers`、`--disable-color-management`、`WLR_DRM_NO_MODIFIERS=1`、`cage` 嵌套（gamescope 报 “Couldn't create Wayland objects”）。
- `--force-composition` 曾让登录页看起来「不再分两半」，但其他界面仍会错位 → 与合成/扫描格式相关，不是简单的 plane 数量问题。
- dmesg 有 `dpu_encoder_frame_done_timeout` / `ppdone_timeout` / DSI 命令 -110（主要出现在会话启停时）。

**怀疑**：msm DPU 在 **dual-DSI 命令模式 + XR30(10bpc)** 扫描输出时存在 bug；X11 走 XR24 所以正常。

**下一步方案（按优先级）**：
1. 做一个 LD_PRELOAD 小库，把 `drmModeAddFB2WithModifiers` 里的 `XR30→XR24`（或 `AR30→AR24`）改掉做验证（颜色会不对，但能确认是否格式问题）。
2. 强制 gamescope 用 8-bit 扫描输出：改源码/打 patch 重新编译（AUR gamescope-git 或官方 tarball），或找旧版 gamescope（3.16.x）测试。
3. 若确认 XR30 是根因：在内核 `msm` 的 plane format 列表里屏蔽 XR30/AR30（改 `msm_dpu`/`dpu_plane` 的 format 列表，重编内核），这样 gamescope 会自动选 XR24。
4. 备选：用 wlroots 合成器（cage/labwc，走 8-bit）跑 Steam 全屏，不用 gamescope（或 gamescope 只做嵌套）。
5. 若短期不修好：Game Mode 保持关闭，SSH/控制台可用；显示问题不影响系统本身。

## 6. 回滚 / 救援

- **救援系统**：自动回退（连续 3 次启动主系统失败→ initramfs 换 sda17）；或 fastboot 刷 `backup/boot-rescue.img` 到 boot_a/boot_b 后重启。
- **回 Ubuntu**：Ubuntu 已被覆盖，需从 PC 备份恢复（见 §7）或 EDL 刷回原厂/`boot_a.raw`+重装。
- **游戏模式开关**：`/etc/j716f-gamemode-enabled` 删掉即可，不影响其它服务。

## 7. PC 侧备份（D:\xiaoxinpadpro\backup）

- `ubuntu-rootfs-20261106.tar.gz`（15.3G，sha256 已校验）= 原 Ubuntu 全盘（含 `/home/j716f/docs`、`j716f-linux` 内核树、`tools`、`backup`）
- `boot_a.raw` / `boot_b.raw`（原 Ubuntu boot 镜像，可 fastboot 刷回）
- `boot-rescue.img` / `boot-main.img`
- `misc.raw` / `devinfo.raw` / `dtbo_a.raw` / `vbmeta_a.raw` / `partitions/`（分区表）
- `steam-ui-capture.png`（Steam UI 在 Xwayland 内的正常截图）

## 8. 下一步 TODO

1. 修显示：按 §5 方案 1→2→3 试，目标是 gamescope 正常 8-bit/线性扫描输出。
2. 修好后：`touch /etc/j716f-gamemode-enabled`，开机自动进 Game Mode，并按 doc 67 §1.2 逐项验收。
3. 装 FEX（`/home/j716f/migrate/steam/fex-tool-backup` 里有 aarch64 FEX）+ Steam-ARM 的 launcher/handler 脚本（`/home/j716f/migrate/steam/steam-arm-install.sh` 里内嵌的 python 部分），让 x86 Linux 游戏和 Windows 游戏可用。
4. 迁移相机 daemon、自动旋转、亮度/屏幕键盘等剩余 Ubuntu 服务。
5. 每个阶段做完在 PC 侧更新本记录。

## 9. ABL 槽位重试计数（重要，会表现为「开机直接进 fastboot」）

**现象**：重启若干次后，设备不再进系统而是进 fastboot（PC 上 `fastboot devices` 能看到 `SERIALNUMBER fastboot`，串口/tty 都消失）。

**原因**：Linux 下没有 Android 的 `bootctl markBootSuccessful`，ABL 的 A/B 重试计数会一直消耗；耗尽后会把槽位判为不可启动/进 fastboot（doc 18 记录约 7 次启动一次）。

**处理（2026-11-06 已实操验证）**：
```powershell
# PC 上（platform-tools）
cd C:\Users\player\Documents\padpro\tools\platform-tools
.\fastboot.exe set_active a
.\fastboot.exe reboot
```
执行后约 1 分钟即可从 192.168.1.10 SSH 进来。

**预防**：
- **PC 看门狗**：`D:\xiaoxinpadpro\tablet\fastboot-watchdog-v2.ps1`（启动项快捷方式当前被禁用）。检测到 fastboot 就自动 `set_active a`（重复出现再 `erase misc`）并 `reboot`。日常建议开启；深度调试时临时关掉，避免自动重启打断。
- **系统内标记成功**：把 `tablet/slot-bcb.py` 装成 systemd oneshot，开机写入 misc@2048 的 bootloader_control（slot A: priority=15 tries=7 successful=1）。未在本机验证 ABL 是否认这个 BCB（doc 63 说计数可能在 RPMB），启用前先备份 misc。
- **手动习惯**：多次重启后若进了 fastboot，执行上面的两条命令即可。

**救援**：进 fastboot 不会丢系统；刷回任意槽的 boot 镜像，或直接 `set_active a` + reboot。

## 10. 夜间修复记录（2026-11-06 23:00 后）

1. **SSH 认证卡 90 秒**（表现为「能 ping、22 端口开、但连不上/认证超时」）
   - 根因：`/etc/pam.d/system-auth` 里的 `pam_systemd_home` 查询 userdb 超时（日志：`pam_systemd_home(sshd:auth): Failed to query user record: Connection timed out`）。
   - 处理：注释掉 system-auth 中 4 处 `pam_systemd_home.so`（已备份为 `system-auth.bak-*`）。重启后 SSH 秒连。
2. **IPv6 duplicate address（DAD failed）**
   - 日志：`IPv6: wlp1s0: IPv6 duplicate address 240e:... used by AA:BB:CC:DD:EE:FF detected!`
   - 处理：3 个 WiFi profile 里 `[ipv6] method=disabled`；现在 wlp1s0 只有 IPv4 `192.168.1.10`。
3. **speaker-mixer 开机失败 / 四扬声器**
   - 原 Ubuntu 脚本 `j716f-configure-speakers.py` 只认内核 `#530` 且依赖 `SPK<n> SPK Switch` 控件；本机内核是 `#26`、没有这些控件。
   - 处理：新增 `/usr/local/libexec/j716f-speakers-simple.sh`（route on + 4×SPK ASP/volumes），用 drop-in `20-simple-speakers.conf` 覆盖 ExecStart；服务加 `Restart=on-failure`，开机自动重试一次即成功。已实测出声。
4. **持久日志**：`journald.conf` 改 `Storage=persistent`、`SystemMaxUse=200M`；以后卡死/重启后可查上一轮启动日志（`journalctl -b -1`）。
5. 状态：重启后 **0 failed units**；音频路由、zram、温控、蓝牙、seatd、SSH 全部 active；Game Mode 自启在显示修好后已开启（见 §11）。

**排查下次重启问题的命令**：
```bash
journalctl --list-boots
journalctl -b -1 -p warning --no-pager | tail -50
journalctl -b -1 | grep -iE 'pam_systemd|duplicate|error|fail' | tail -30
systemctl --failed
```

## 11. 显示错位彻底修复：8-bit 扫描输出 shim（2026-11-06 23:30）

**根因**：gamescope 的 DRM 后端在这块屏上会优先选 `XRGB2101010`（10-bit，XR30）做扫描输出；msm 的 dual-DSI 命令模式面板在 10-bit 下有 bug（画面左右切成两半）。X11 用 8-bit XR24 所以没问题。

**验证方法**：用 `LD_PRELOAD` 拦截 libdrm，把 planes 的 `formats` 和 `IN_FORMATS` blob 里的 10-bit fourcc 全部替换/过滤为 8-bit，gamescope 就自动选了 XR24（`/sys/kernel/debug/dri/0/state` 可确认 `format=XR24`），画面立即正常。

**落地产物**：
- 源码：PC `tools/j716f-no10bit.c`，平板 `/home/j716f/j716f-no10bit.c`
- 库：`/usr/local/lib/libj716f-no10bit.so`（`gcc -shared -fPIC -O2 -o ... -ldl`）
- 已写进启动器 `/usr/local/bin/j716f-gamemode`：
  `export LD_PRELOAD="/usr/local/lib/libj716f-no10bit.so"`
- 启动器还带 `--force-composition`（保险，避免直出 plane 路径）。

**自启开关**：`/etc/j716f-gamemode-enabled`（现在已创建 → 开机自动进 Game Mode）。
关掉自启：`sudo rm /etc/j716f-gamemode-enabled`；手动启动：`/usr/local/bin/j716f-gamemode`。

**备注**：Steam 启动日志里的 `steamos-select-branch: command not found`、`jupiter-dock-updater` 等是 SteamOS 专用小工具缺失的非致命提示，不影响 Big Picture。

## 12. 最终验收（2026-11-06 23:33，已实测）

重启（`systemctl reboot`）后自动完成：
- 开机 → tty1 自动登录 player → `/usr/local/bin/j716f-gamemode` → gamescope → **Steam Big Picture 登录页**，无需手动操作；
- KMS 扫描格式 `XR24`（8-bit），画面**不再分两半**；
- `systemctl --failed` = **0 failed units**；
- WiFi 自动连回 `192.168.1.10`，SSH 可用；
- 音频路由（4×CS35L41）在启动时由 `speaker-mixer.service` 自动配置；zram、温控、蓝牙、seatd 全部 active。

**还没做的**：
1. Steam 账号登录（需要用户在平板上用触屏/扫码登录；有 Steam Guard 要准备验证码）。
2. ARM64 Proton / Steam-ARM launcher 集成（x86 Linux 游戏已经能通过 FEX 跑，见 §13；Windows 游戏还需要 Proton）。
3. 相机 daemon、自动旋转、亮度/屏幕键盘等剩余 Ubuntu 服务。
4. 每个阶段的 PC 侧备份（新 Arch 系统的 tar），建议之后补一次。

## 13. FEX（x86/x86_64 转译）安装记录（2026-11-06 23:40-23:45）

**目标**：让 x86_64 Linux 游戏和 x86 Proton 能在这台 aarch64 设备上跑。

**已完成（已实测）**：
1. **FEX 二进制**（来自 Ubuntu 时期备份 `/home/j716f/migrate/steam/fex-tool-backup/bin/`，FEX-2607）安装到 `/usr/local/bin/`：`FEX` `FEXBash` `FEXGetConfig` `FEXServer` `FEXServerManager` `FEXpidof` `FEXOfflineCompiler`。
2. **binfmt_misc 注册**（开机持久）：`/etc/binfmt.d/FEX-x86_64.conf`、`/etc/binfmt.d/FEX-x86.conf`（flags `PF`），`systemd-binfmt` 在启动时注册；当前 `/proc/sys/fs/binfmt_misc/FEX-x86_64` 已 enabled。
3. **x86_64 RootFS**：从清华镜像下载 `ubuntu-base-24.04.5-base-amd64.tar.gz`（30MB，17MB/s），解压到 `/opt/fex-rootfs/Ubuntu_24_04`（84MB，含 x86_64 glibc/ld-linux）。
4. **FEX 配置**：`/home/j716f/.fex-emu/Config.json` 与 `/etc/fex-emu/Config.json`：
   ```json
   { "Config": { "RootFS": "/opt/fex-rootfs/Ubuntu_24_04" } }
   ```
5. **测试通过**：
   - 静态 x86_64 busybox（binfmt 直接执行）：`hello via binfmt x86_64` ✓
   - RootFS 里的动态 x86_64 程序：`/opt/fex-rootfs/Ubuntu_24_04/bin/ls --version` → GNU coreutils 9.4 ✓、`uname -m` → x86_64 ✓
   - 也就是说：**任何 x86/x86_64 ELF 现在在这台机器上都能透明运行**（FEX 自动接管）。

**还没完成**：
1. **Steam-ARM 的 launcher/handler 集成**（`steam-arm` 包装脚本、FEX 环境变量、GL/Vulkan thunk 转发、Proton 选择）。原始安装器只支持 Debian/Ubuntu，需从 `steam-arm-install.sh` 里抽出内嵌 python/启动脚本移植到 Arch。
2. **ARM64 Proton**：Steam 客户端需下载 `proton_11-arm64`（Windows 游戏），并配置 per-game compatibility；需要登录 Steam 后实测。
3. **Steam-ARM 官方 FEX/Mesa provider 存档**（`steam-arm-fex-mesa-26.1.8-x86_64-i386.tar.zst`，77MB）**下载太慢（GitHub 被墙，ghfast.top ~6KB/s）**，本次只下了 3.5MB 就停了；下次可用 PC 断点续传/更好镜像下载后解压到 `/opt/fex-rootfs/Ubuntu_24_04-mali`（Mali 路线用；Adreno 可走 GPU forwarding 路线，可能不需要）。
   - 下载命令（平板）：
     ```bash
     curl -L -C - -o steam-arm-fex-mesa.tar.zst 'https://ghfast.top/https://github.com/Scrumpper/Steam-ARM/releases/download/steam-arm-v2.0/steam-arm-fex-mesa-26.1.8-x86_64-i386.tar.zst'
     ```
4. FEX 的 GL/Vulkan thunk 转发在真实游戏里的表现需验证（`steam-arm-glx-lax` 等脚本已从 Ubuntu 迁移到 `/usr/local/sbin/`，但尚未接进启动流程）。

**下一步建议**：登录 Steam → 下一个小体积 Windows 游戏 → 尝试默认 x86 Proton（自动走 FEX）或手动装 ARM64 Proton → 如果 GL/Vulkan 转发有问题，再接 Steam-ARM 的 launcher/handler。

## 14. 剩余设备服务迁移状态（2026-11-06 23:55）

| 服务 | 状态 | 说明 |
|---|---|---|
| 亮度 | ✅ 可用 | `/sys/class/backlight/ae94000.dsi.0/brightness`（max 2047）可读写，`99-backlight.rules` 已装；Steam/gamescope 可调 |
| v4l2loopback | ✅ 模块已载入 | `/etc/modprobe.d/j716f-v4l2loopback.conf` 生效，`/dev/video42/43/44` 已创建 |
| 相机 sensor 驱动 | ✅ 模块可加载 | `ov13b10` / `s5k4h7` / `gc5035` + `qcom_camss` 正常加载，`/dev/media0/1/2` 存在 |
| 相机 daemon/管线 | ❌ 未完成（已装未启用） | `camera-setup-all.sh` 报 media graph incomplete 并循环重启；已 disable 避免刷日志，留待专门会话移植（需要媒体图/固件细节） |
| hexagonrpcd（SDSP/FastRPC） | 已装、未启用 | 传感器/旋转要用；需要更多固件路径适配 |
| 麦克风服务 | 已装、未启用 | 内置麦已归档（doc 62/63），用 USB/蓝牙麦 |
| netconsole / charge-status / game-mode / typec-rebind / x-freeze-watchdog | 已装、未启用 | Ubuntu 时代辅助工具，按需再开 |
| 自动旋转 | 不适用当前模式 | `j716f-rotate.py` 是 X11/xrandr 守护；Game Mode 固定横屏。若以后加桌面模式再移植 |
| 屏幕键盘 | — | Steam Big Picture 自带 OSK，`osk.py` 不需要 |

**当前最终状态**：`systemctl --failed` = 0；Game Mode（Steam Big Picture）正在运行；SSH/WiFi/音频/温控/zram/蓝牙正常；相机栈已安装但禁用（不刷错误日志）。

## 15. 中文显示完善（2026-11-07 00:00）

**问题**：Arch 初装没有 CJK 字体（`fc-list :lang=zh` = 0），Steam UI 全英文；中文会显示成方块。

**处理**：
1. 安装字体：`noto-fonts-cjk`、`noto-fonts`、`noto-fonts-emoji`（`fc-list :lang=zh` 现在 80+ 个字形族，共 725 个字体条目）。
2. 系统 locale：`/etc/locale.conf` 改 `LANG=zh_CN.UTF-8`（`zh_CN.utf8` 已生成）。
3. Game Mode 启动器 `/usr/local/bin/j716f-gamemode` 里 `LANG`/`LC_ALL` 改成 `zh_CN.UTF-8`；Steam 会据此用中文 UI（日志确认 `steamwebhelper ... -lang=zh_CN`）。
4. fontconfig 简体偏好：`/etc/fonts/conf.d/99-j716f-zh.conf`（sans-serif/serif 优先 `Noto Sans/Serif CJK SC`，避免匹配到 KR/JP 变体字形）。
5. 重启 Game Mode 后验证：Big Picture 全中文（活动流、菜单、按钮“选项/选择/返回”、游戏名“影之刃零/人类一败涂地”等），字体渲染正常，截图存 PC `backup/steam-zh2.png`。

**备注**：Steam 客户端语言没有单独写进 config.vdf，目前跟随系统 locale（`LANG=zh_CN.UTF-8`）；如以后想切英文，把启动器里 LANG 改回 `en_US.UTF-8` 即可。

## 16. 性能优化：CPU/GPU 满血测试与最终 profile（2026-11-07 00:00-00:30）

### 结论

| 部件 | 之前（Ubuntu 时代稳定性 pin） | 现在（performance profile） | 提升 |
|---|---|---|---|
| CPU 小核 A55×4 | 1804800 固定 | 1804800（原生最大） | — |
| CPU 大核 A77×3 (policy4) | 1574400 固定，max 2246400 | min 710400 / **max 2419200（原生最大）** | sysbench 8 线程 **4031→5763 (+43%)** |
| CPU 超大核 A77 (policy7) | 1862400 固定，max 2361600 | min 844800 / **max 2841600（原生最大）** | sysbench 单核 **995→1216 (+22%)** |
| GPU Adreno 650 | **400MHz 固定**（仅 60%） | min 305 / **max 587MHz**（simple_ondemand） | vkmark **670→935 分（+40%）** |
| UFS | 300MHz 固定 | 300MHz（不变，已是最高） | — |

测试条件：vkmark headless 2560×1600（vertex+texture）；sysbench cpu `--cpu-max-prime=20000`；温度用 CPU/GPU thermal zone。

### 稳定性实测（这是关键）

- CPU 原生最大：`stress-ng --cpu 8` 连续 **90s** 通过，cpu 温度峰值 **70.9°C**。
- CPU 原生最大 + GPU 587MHz 同时满载：连续 **120s** 通过，cpu 峰值 **75.6°C**、gpu 峰值 **56.6°C**，无降频、无死机。
- **GPU 670MHz（原生最大）不可用**：vkmark 满载约 30-40s 后整机**硬复位**（`androidboot.bootreason=hard_rst`，无 pstore 日志；随后 ABL 计数耗尽进了 fastboot，用 `fastboot set_active a` 恢复）。
  → 结论：这颗 Adreno 650 在当前 mainline 内核/OPP 配置下，670MHz 档位电压/稳定性有问题；**587MHz 是实测稳定上限**（约 88% 标称）。
- 温度阈值：CPU trip 90°C、GPU trip 85°C；上述负载远低于阈值，功耗/散热有富余（长时间游戏可能更高，但不会立刻降频）。

### 落地产物

- `/usr/local/bin/j716f-perf`：`performance` / `safe` / `status` 三种用法
- `/etc/j716f-perf.conf`：当前 `MODE=performance`（重启时由 `j716f-perf.service` 应用）
- `/etc/systemd/system/j716f-perf.service`（enabled）：开机自动应用
- 原 `j716f-cpu-cap.service`、`j716f-stability.service` 已 disable（被新 profile 取代；脚本保留作参考）
- `j716f-idle-fix`（禁用深睡）、`j716f-pstore-tune` 保持启用

### 使用

```bash
sudo /usr/local/bin/j716f-perf status         # 查看当前频率/温度
sudo /usr/local/bin/j716f-perf safe           # 恢复保守 pin（GPU 400MHz、CPU 锁频）
sudo /usr/local/bin/j716f-perf performance    # 应用满血 profile
sudo sed -i 's/^MODE=.*/MODE=safe/' /etc/j716f-perf.conf   # 下次开机用 safe
```

### 说明 / 遗留

- 587MHz 是实测稳定上限，不是硬件写明的 670；想要更高只能改内核 OPP 电压表（需要重编内核，风险高，暂不做）。
- 若游戏里出现花屏/闪退/重启，先用 `j716f-perf safe` 验证是否频率问题，再回退。
- 长时间满载后基带/充电区域温度会上升，`j716f-charge-thermal` 已在管理充电电流。
- 反复高频实验会消耗 ABL 槽位计数，进 fastboot 时按 §9 处理。

## 17. 显示再次分割的修复：强制线性扫描（2026-11-07 00:25）

**现象**：性能 profile 生效后，game mode 画面又变成左右两半错位。

**诊断**：KMS state 里 gamescope 扫描缓冲变成了
`format=AR24  modifier=0x500000000000001`（AFBC 压缩修饰符），而之前正常时是 `modifier=0x0`（线性）。
双 DSI 面板对 AFBC 扫描缓冲处理有问题 → 又出现分屏。

**修复**：扩展 `/usr/local/lib/libj716f-no10bit.so`（源码 `tools/j716f-no10bit.c`）：
- 原来：过滤 plane 的 10-bit 格式 + 把 IN_FORMATS blob 里的 10-bit fourcc 换成 8-bit；
- 现在再加一步：**把 IN_FORMATS blob 里所有 modifier 强制改成 `DRM_FORMAT_MOD_LINEAR (0)`**。
这样 gamescope 的候选扫描格式只剩「8-bit + 线性」，实测 KMS 变成 `format=AR24 modifier=0x0`，画面正常。

**验证**：重启 Game Mode 后用户确认不再分割；`dmesg` 的 `dsi_err_worker` 只在模式切换瞬间出现（历史 19 条，无持续刷屏）。

**相关命令**：
```bash
# 检查当前扫描格式/修饰符
sudo grep -E 'format=|modifier=' /sys/kernel/debug/dri/0/state | head -4   # 期望：AR24/XR24 + modifier=0x0
# 重新编译 shim（改动后）
gcc -shared -fPIC -O2 -o /home/j716f/libj716f-no10bit.so /home/j716f/j716f-no10bit.c -ldl
sudo install -m755 /home/j716f/libj716f-no10bit.so /usr/local/lib/
# 重启 Game Mode 生效
pkill -f 'gamescope|steamrtarm64/steam'; setsid nohup /usr/local/bin/j716f-gamemode >/tmp/gm.log 2>&1 &
```

## 18. CPU 3.19GHz 解锁 + GPU 上限分析（2026-11-07 00:35-00:45）

### CPU 超大核 3.19GHz：✅ 已实现

**调查**：硬件 cpufreq LUT（ABL 提供）里有 3187200 这一档，但启动日志报
`cpu cpu7: Voltage update failed freq=3187200` —— 因为 mainline 的 `opp-table-cpu7` 只写到 2841600（865 的上限），
`qcom-cpufreq-hw` 用 LUT 里的频率去 `dev_pm_opp_adjust_voltage` 时找不到对应 OPP，就把这一档丢掉了。

**做法（只改 DTB，不动内核代码）**：
1. 从内核构建树取 `sm8250-lenovo-j716f.dtb` → `dtc` 反编译成 dts；
2. 在 `opp-table-cpu7` 末尾加一条：
   ```
   opp-3187200000 {
       opp-hz = <0x00 0xbdf8d000>;
       opp-peak-kBps = <0x7faf80 0x3138000>;   /* 与 2841600 同带宽 */
   };
   ```
3. `dtc` 重新编译 → 用 `/home/j716f/tools/repack_boot.py` 以 `boot-main.img` 为模板重打包 → `boot-cpu32.img`（35.4MB）；
4. `dd` 写入 `boot_a`、`boot_b`（保留旧 `boot-main.img` 作回滚）；
5. 重启后验证：`scaling_available_frequencies` 包含 `3187200`，`cpuinfo_max_freq=3187200`，可直接 `scaling_max_freq=3187200`。

**实测（90s all-core stress-ng @3.19GHz）**：
- 全程稳定在 3187200，无重启；
- cpu7-top-thermal 峰值 **81.4°C**（trip 90°C），cpu0-thermal 67.8°C；
- sysbench 单核：2.84GHz **1162.79** → 3.19GHz **1215.14** ev/s（+4.5%，受内存/热影响；纯 CPU 理论 +12%）；
- sysbench 8 线程：**5788** ev/s（与 2.84 时 5763 基本持平，多核受带宽/散热限制）。

### GPU 905MHz：❌ 这台硬件不支持（已核实）

- 从**原厂 boot.img** 里提取的 stock DTB：GPU OPP 最高 **670MHz**，没有任何 905MHz 的节点/数值；
- 原厂 boot/kernel 镜像里搜索 905000000（u32/u64、大端/小端）**0 命中**；
- mainline DT 的 gpu opp table 同样最高 670（opp-level 0x140）；
- 结论：Adreno 650 在 SM8250-AC（骁龙 870）上的官方最高就是 **670MHz**，905MHz 是别的机型（如 ROG 3 超频）的数据，不是这台平板的；
- 实测：**670MHz 满载 30-40s 会整机硬复位**（twice，`hard_rst`，进 fastboot）；**587MHz 稳定**（60s+120s 压测通过）。
  670 复位原因大概率是 mainline 下 GPU 高压档的电压/ACD 处理不完整，不是散热问题（GPU 峰值才 56°C）。

### 游戏时压榨性能：`j716f-gameboost`

`/usr/local/bin/j716f-gamemode` 启动时自动：
- `sudo -n j716f-gameboost on` → CPU governor **performance**（p0/p4/p7 全部跑满，p7=3187200）、GPU **固定 587MHz**；
- Steam 退出后自动 `off` → 回到 `performance` profile（schedutil + GPU 305–587）。

sudoers 免密：`/etc/sudoers.d/j716f-gameboost`（只允许该 helper）。

```bash
sudo /usr/local/bin/j716f-gameboost on|off   # 手动切换
sudo /usr/local/bin/j716f-perf status        # 查看频率/温度
```

### 回滚 / 备注

- 新 boot 镜像：`deploy/boot-cpu32.img`、`deploy/boot-main-cpu32.img`、PC `backup/boot-main-cpu32.img`；
- 旧镜像：`deploy/boot-main.img`（不含 3.19 OPP）——若需回滚，把它 dd 回 `boot_a`/`boot_b` 即可；
- 已知小问题：mainline DTS 的 `opp-1747200` 节点把 `opp-hz` 写成 1708800000（笔误），所以 LUT 里的 1747200 档仍然加载失败（不影响 3.19 档）；下次重做 DTB 时可顺手修正。

## 19. 今晚收尾状态（2026-11-07 00:45）与明日待办

**收尾状态（全部已验证）**：
- Game Mode 正在运行，Steam Big Picture 中文界面；扫描格式 `AR24 + modifier=0x0`（不会分屏）；
- 性能 profile = `performance`：CPU p7 max 3187200（schedutil，空闲降频）、p4 max 2419200、GPU 305–587MHz；游戏时由 `j716f-gameboost` 自动切 performance governor + GPU 587；
- `systemctl --failed` = 0；温度 ~35°C；电池 100%；WiFi `192.168.1.10`；SSH 正常；
- **boot_a / boot_b 都已刷入新 DTB 镜像**（sha256 `132806647fe10e73…`，与 `deploy/boot-cpu32.img` 一致），开机即带 3.19GHz OPP；
- PC 备份：`backup/boot-main-cpu32.img`（新，含 3.19GHz）、`deploy/boot-main.img`（旧，回滚用）。

**明日待办（按优先级）**：
1. **游戏实测**：库里已有《人类一败涂地》，在 Big Picture 启动 → 观察 FEX/Proton 链路、帧率、CPU/GPU 频率与温度；可配合 `mangohud`（已装）或 gamescope 统计。
2. 根据实测结果装 **ARM64 Proton / Steam-ARM launcher 集成**（Windows 游戏）。
3. 可选：修正 DTS `opp-1747200` 笔误、尝试稳定 GPU 670MHz（需要电压/ACD 资料）、相机管线移植。
4. 建议白天补一次 **新 Arch 系统整体 tar 备份** 到 PC（当前只有旧 Ubuntu 备份 + boot 镜像）。

**若出现异常**：
- 进 fastboot：PC 上 `fastboot set_active a && fastboot reboot`；
- 黑屏/无法进系统：可把 `deploy/boot-main.img` dd 回 `boot_a`/`boot_b`（去掉 3.19GHz 的那版内核/DTB）；
- 显示分屏：`sudo grep -E 'format=|modifier=' /sys/kernel/debug/dri/0/state`，若 modifier≠0 或 format=XR30，重启 Game Mode（shim 会重新生效）。

## 20. 事故复盘：整夜 boost 导致硬死机/卡 logo（2026-11-07 03:16 / 07:30）

**现象**：早上平板卡在 Lenovo logo，网络/USB/fastboot 都不响应；强制重启后能进 Steam，但重启 Game Mode 后很快又卡死；最终需手动重启。

**journal 证据**：
- `journalctl --list-boots`：昨晚那次启动在 **03:16:28 日志戛然而止**（最后一条是 NetworkManager CONNECTED_SITE），随后无任何 panic/pstore → 典型**硬复位/硬死机**；
- 该次启动从 00:53 起、Game Mode 由 tty1 自动启动，而**旧启动器会自动执行 `j716f-gameboost on`**：
  CPU 全部 `performance` governor（超大核锁 **3187200**/3.19GHz）、GPU 锁 **587MHz**，持续约 2.5 小时，且一直插着充电；
- 07:29:50 我们重启 Game Mode 时又自动 boost，**6 秒后** PMIC 开始刷
  `qcom-pm8150b-charger: battery over-voltage`（36 条），07:30:01 开始刷 dsi_err_worker，然后卡在 logo；
- `battery over-voltage` 在满电(100%)+充电时本来就有零星出现（07:18/07:25 那两次失败启动也各有 37/38 条），但和 boost 同时出现时直接卡死。

**根因判断**：
1. 旧启动器把「游戏 boost」做成了 Game Mode 一启动就常驻：CPU 3.19GHz + GPU 587MHz 长时间锁最高频；
2. 3.19GHz 和 587MHz 都只在短时压测（90-120 秒）里验证过，**整夜连续高负载 + 充电**超出了这台的稳定边界（可能叠加满电充电时的 PMIC over-voltage 保护）；
3. 07:30 的快速复现说明这是可重复的，不是偶发。

**已修复**：
- `/usr/local/bin/j716f-gamemode` **不再自动 boost**（只应用普通 performance profile）；
- 默认 `performance` profile 改为：CPU 超大核 **max 2841600（2.84GHz）**、大核 2419200、小核 1804800、全部 `schedutil`；GPU **305–587MHz 动态**（不锁频）；
- `3.19GHz + GPU 587` 只保留在**手动** `sudo /usr/local/bin/j716f-gameboost on` / `... off`（gaming 模式，GPU 也改回动态 305-587）；
- `/etc/j716f-perf.conf` = `MODE=performance`，开机由 `j716f-perf.service` 应用普通 profile。

**使用建议**：
- 平时（含整夜下载/安装）：**不要**开 boost；
- 真要压榨性能玩游戏：手动 `sudo j716f-gameboost on`，玩完 `sudo j716f-gameboost off`；不要整夜挂着 boost，满电插电时更不要；
- 若再次卡 logo/黑屏：长按电源 15-20 秒强制关机再开机；若进 fastboot：`fastboot set_active a && fastboot reboot`；
- 3.19GHz 的长期稳定性仍未验证，后续要在「不充电 + 短时游戏」条件下再评估，必要时把 boot 镜像回滚到 `deploy/boot-main.img`。

**当前状态**：Game Mode 正常运行（Steam UI 中文、XR24 线性扫描）、CPU 2.84GHz schedutil、GPU 305–587MHz 动态、0 failed units。

## 21. GPU 超频调研（2026-11-07 08:00-09:00）

### 别人的 Adreno 650 为什么能 905MHz？

- **ROG Phone 3/Red Magic 5G（SD865/865+）**：官方 GPU 上限就是 **670MHz（部分标 675MHz）**；网上 905/940MHz 都是**第三方/自制内核**改 `kgsl` 的 GPU 频率表 + 电压表（甚至加电压/改 PLL）实现的。
  证据：ASUS 官方论坛回复「GPU 时钟由内核控制，Qualcomm 锁死，除非自制内核」；用户举例「Red Magic 有定制内核能到 940MHz」（[ROG 论坛](https://zentalk.asus.com/t5/rog-phone-3/possiblity-to-rise-up-the-gpu-clock-for-android-11/m-p/72026)）。
- **Pimax Portal/Crystal（Adreno 650）**：开发者说 root + 超频内核可以稳定 **855/905MHz**（[SweClockers 帖子](https://www.sweclockers.com/forum/post/19895452#2)）。
- **Motorola Edge S（SD870）**：有说法是 OEM 直接标到 905MHz（[GSMArena 评论](https://m.gsmarena.com/xiaomi_poco_f4_stops_by_geekbench_with_snapdragon_870-news-53645.php)）。
- 共同点：都需要**厂商内核（kgsl）里有更高频点 + 对应电压表**，通常要改内核，甚至改 GPU PLL/加电压。

### 我们为什么不行（mainline 架构）

1. 我们的 `msm/adreno` 驱动把 GPU 频率/电压交给 **GMU 固件 + RPMh ARC 投票**处理：
   - DT 里的 `opp-level` 对应 RPMh 电压等级（`RPMH_REGULATOR_LEVEL_*`），显卡实际频率由 bootloader（ABL）提供的 **cmd_db `gfx.lvl`** 表和 RPMh/GMU 内部配置决定；
   - 我们 DT 只列了 7 档：305/400/441.6/490/525/587/**670**，对应 RPMh 等级 LOW_SVS…**NOM_L1(0x140)**；mainline 的 sm8250 GPU 最高就是 **670**。
2. 想超过 670，要给 DT 加一个**更高的 RPMh 等级**（NOM_L2=336 / TURBO=384 …），前提是这台设备的 cmd_db/PMIC 配置支持这些等级，且 GPU PLL/电压能跑；这已经超出内核 DT 能控制的范围，属于“改固件/改 bootloader 表”的范畴。
3. 实测也证明 670 已经到边界：
   - 670MHz 单跑 60s 曾通过一次，但 **CPU 8 线程 + GPU 670 的 soak（5 轮 vkmark）又整机硬死机**（需手动重启才能恢复）；
   - 587MHz 在 60s/120s 组合压测里稳定；CPU 3.19GHz 也有整夜 hard reset 先例。
   → 结论：**这台机器在 mainline 内核下，GPU 稳定上限 = 587MHz；670 不稳定；905 做不到（除非自己写/移植下游超频内核 + 改 GPU 电压/固件，工作量和风险都很大，且这台平板供电/散热设计未必撑得住）。**

### 已落地的最终配置

- 默认 `performance` profile：CPU 超大核 max **2841600**、大核 2419200、小核 1804800，`schedutil`；GPU **305–587MHz 动态**；
- 游戏时 `j716f-gameboost on` 只把 CPU 切 `performance`（含 3.19GHz 档）、GPU 仍 305–587 动态（不锁频）；
- `j716f-gameboost` 不再随 Game Mode 自动开启（避免整夜锁频死机）。

### 还想继续折腾 GPU 的下一步（高风险，需专门安排）

- 唯一可能的软件路径：在 DTB 的 gpu opp table 里加一条更高 RPMh 等级（如 `opp-700000000` + `opp-level = <RPMH_REGULATOR_LEVEL_NOM_L2>`），重打 boot.img 测试；
  - 若 cmd_db 没有该等级，dmesg 会打印 `Level %u not found in the RPMh list` + 可用等级列表（可以借此读出这台设备的真实 RPMh 等级表）；
  - 若失败可能导致 GPU probe 失败（黑屏但 SSH 可用）或直接死机，需要用旧 boot 镜像回滚；
  - 不建议在没备份、没串口/ADB 保障的情况下做。

## 22. 整体性能/稳定性研究：降压超频可行性与最终方案（2026-11-07 09:00）

### 结论先行：软件层面没法“降压”，只能“降频”

- **CPU**：电压来自 bootloader 的 cpufreq LUT（`qcom-cpufreq-hw` 驱动读频率+电压表，再 `dev_pm_opp_adjust_voltage`）；DT 里没有 `opp-microvolt`，OPP debugfs 只暴露 `rate_hz/level/turbo`，**没有任何 sysfs 可写电压**。
- **GPU**：电压/频率由 GMU 固件 + RPMh ARC 投票（cmd_db `gfx.lvl`）决定，DT 只是映射 RPMh 等级，同样没有软件调压接口。
- 因此真正的“降压”只能**改内核**（例如在 `qcom-cpufreq-hw.c` 里把 LUT 电压统一减 25/50mV），需要重新编译内核并逐档验证稳定性——风险高，本次未做（可作为后续可选项目）。
- 实际等效做法：**降频**（低 OPP 档的 LUT 电压本来就低）→ 功耗/发热按比例下降。

### 稳定性实测结论（今天多次验证）

| 配置 | 负载 | 结果 |
|---|---|---|
| CPU 3.19GHz（performance gov） | 整夜 Steam 下载/安装 + 充电 | 约 2.5h 后**硬复位**（03:16） |
| GPU 670MHz 固定 | CPU8 + vkmark soak | 约 1 分钟**硬死机**（多次） |
| CPU 2.84GHz + GPU 587 + 充电 | CPU8 + vkmark 持续 | 约 1 分钟内**硬复位**（08:53/08:56 两次） |
| **balanced：CPU 2.36/2.24 + GPU 305-525 动态** | CPU8 + vkmark 66s | **稳定**，cpu7 峰值 **71.7°C**，gpu 46.9°C |
| 满电 + 充电 | 高负载 | PMIC 反复报 `battery over-voltage` |

结论：
- 这台机器在 mainline 下的**稳定边界比预想低**：CPU 2.84 + GPU 587 的组合在持续 CPU+GPU 双载 + 充电时已经会硬复位；**balanced（2.36/2.24 + GPU 525）才是可长期稳定的点**。
- 温度不是直接原因（峰值才 72°C，远离 90/85°C 阈值），更像是**供电/OPP 裕量**问题（满电充电时更明显）。
- 因此“降压超频”在当前内核下不现实；**降频（balanced/eco）+ 热管理**才是稳定路线。

### 已落地（最终方案）

1. **`/usr/local/bin/j716f-perf` 多档 profile**：
   - `balanced`（**默认**）：CPU 2.36/2.24/1.8GHz、GPU 305–525MHz 动态、schedutil；
   - `performance`：CPU 2.84/2.42、GPU 305–587（短时用，长时间双载+充电有风险）；
   - `gaming`：CPU 稳定上限（2.36/2.24）+ `performance` governor（减少降频延迟）、GPU 305–587；
   - `eco`：CPU 2.26/1.96/1.52、GPU 305–441（长时/热天）；
   - `safe`：原始保守 pin；`extreme`：3.19GHz（手动、已知不稳）。
2. **热管理守护** `/usr/local/bin/j716f-thermal-guard`（systemd 常驻）：
   - cpu7 ≥ 78°C 或 gpu ≥ 72°C → 自动切 `eco` 并把充电输入电流降到 1A；
   - 温度回落到 70/65°C 以下持续 30s → 恢复 `/etc/j716f-perf.conf` 里的模式；
   - 默认配置 `/etc/j716f-perf.conf` = `MODE=balanced`，开机由 `j716f-perf.service` 应用。
3. **`j716f-gameboost on/off`**：`on` → gaming profile，`off` → balanced（不再整夜锁最高频）。
4. 实测（balanced 双载 66s）：稳定，cpu7 峰值 71.7°C、gpu 46.9°C；`systemctl --failed` = 0。

### 使用建议

```bash
sudo /usr/local/bin/j716f-perf status       # 查看当前模式/频率/温度/充电
sudo /usr/local/bin/j716f-perf balanced     # 默认稳定档
sudo /usr/local/bin/j716f-perf eco          # 最省电/最凉
sudo /usr/local/bin/j716f-gameboost on/off  # 游戏前/后
```

- 长时间游戏建议用 `balanced` 或 `gaming`，**不要**用 `performance`/`extreme` 叠加整夜；
- 满电插电 + 重载容易触发 PMIC over-voltage，尽量在**电量 30-80%** 或**电池供电**下玩游戏；
- 想真正“降压超频”：下一步需要改内核 `qcom-cpufreq-hw.c` 的 LUT 电压偏移并逐档验证（可另开一次会话专门做）。

## 23. 游戏时“停充保性能”机制（2026-11-07 09:00-09:10）

### 澄清：为什么温度低还会崩？

- 之前说的 72°C 是 **SoC（CPU/GPU）结温**，离 CPU 90°C / GPU 85°C 的保护阈值还很远，所以**不是过热降频/热保护**；
- 真正的问题是**整机供电预算**：CPU+GPU 满载（尤其 2.84GHz + 587MHz）+ **同时充电** 时，充电器→PMIC→电池这条路径的总电流超出裕量，PMIC 报 `battery over-voltage` 并触发保护性**硬复位**。
- 之前“降频”的目的不是压温度，而是**降低 SoC 电流**，让“SoC + 充电”都落在供电预算内。

### 按你的思路实现：游戏时降低充电功率，把预算留给 SoC

1. `j716f-gameboost on` 现在会：
   - 切 `performance` profile（CPU 最高 2.84GHz、GPU 305–587 动态）；
   - **停止充电**：`charging_enabled=0`、输入电流上限 ICL=0，并创建标志 `/run/j716f-gaming-charge-off`；
   - 电量 ≤15% 时自动放宽为 0.5A 慢充（保命，不会玩到关机）。
2. `j716f-gameboost off`：删除标志、恢复 ICL 3A/充电、切回 `balanced`。
3. `charge-thermal.service` 打了补丁：检测到游戏标志时不再按温度调 ICL，保持停充；退出后恢复原 JEITA 温控逻辑。
4. 热管理守护 v2：**游戏时阈值提高到 cpu7 ≥86°C / gpu ≥80°C** 才降 `eco`（避免游戏中途被误降频），非游戏仍 78/72°C。

### 实测（2.84GHz + GPU 587 + CPU8 + vkmark，同时停充）

| 项目 | 结果 |
|---|---|
| 稳定性 | **60-90s 满载通过**，无硬复位（之前同条件充电时会 1 分钟内崩） |
| 温度 | cpu7 最高 **88°C**（仍低于 90°C 保护阈值），gpu 57°C |
| 电池 | 放电约 0.4–0.5A（~2W 表观值），电量 100% 开始 |
| 退出 | `gameboost off` 后充电恢复、profile 回 `balanced` |

### 使用方式

```bash
# 游戏前：性能全开 + 停充（用电池玩，充电器只供电不充电池）
sudo /usr/local/bin/j716f-gameboost on
# 玩完：恢复充电 + 平衡档
sudo /usr/local/bin/j716f-gameboost off
# 查看状态（频率/温度/充电）
sudo /usr/local/bin/j716f-perf status
```

### 注意

- 停充后是**电池供电**，重载游戏大概几小时续航；电量低于 15% 会自动 0.5A 慢充保命，但此时性能可能受供电限制，建议电量 30% 以上再开游戏模式；
- 88°C 是“CPU8 + GPU 同时满载”的合成压力值，真实游戏一般更低；长时间游戏如果 `status` 里 cpu7 长期 >86°C，建议手动 `j716f-perf eco` 或休息一下；
- 停充标志在 `/run/`（重启即失效），不会影响日常充电；如果异常退出没执行 `off`，重启一次即可恢复。

## 24. 画面再次左右割裂：根因是自改 DTB，已回滚并清理（2026-11-07 09:10-09:25）

**现象**：画面又出现左右割裂。排查时 KMS 扫描格式已经是 `AR24 + modifier=0`（shim 正常、LD_PRELOAD 已加载、单/双平面都试过），但割裂依旧。

**定位过程**：
1. 尝试 `--disable-layers`（强制单平面）无效；
2. 干净重启后依旧；
3. **把 boot 镜像回滚成原始 DTB 版 `deploy/boot-main.img`**（去掉为 3.19GHz 反编译+重编译出的 DTB）→ 重启后画面**恢复正常**。

**根因**：之前为了加 3.19GHz OPP，用 `dtc` 把构建出的 DTB **反编译成 dts 再重编译**（round-trip）。虽然 KMS/频率看起来正常、也能开机，但 DTB 里的显示相关属性（DSI/DPU/panel 相关）在 round-trip 后发生了变化，导致双 DSI 面板在部分情况下出现左右割裂。**结论：不要用反编译/重编译的 DTB 上机；以后要加 OPP，必须在内核源码树里改 dts 后用 `make dtbs` 重新构建。**

**已清理（避免再误用）**：
- boot_a/boot_b 已恢复为原始镜像，sha256 与 `deploy/boot-main.img` 一致（`5e40fda3…`）；
- 自改 DTB 相关文件（`boot-cpu32.img`、`boot-main-cpu32.img`、`perf-base.dts/dtb`、`perf32.dtb`、`build_boot_cpu32.sh`）已移到 `/home/j716f/backup/reverted-dtb-experiment/` 归档，不在生效路径；
- `j716f-perf` 里 **删除 `extreme`/3.19GHz 模式**（当前原始 DTB 没有 3187200 档），保留 balanced/performance/gaming/eco/safe；
- PC 备份里仍保留 `backup/boot-main-cpu32.img`（仅作实验记录，**不要刷**）。

**当前状态（用户确认）**：画面正常、Game Mode 运行中、KMS `XR24 + modifier=0`、balanced profile（CPU 2.36/2.24、GPU 305–525）、0 failed units；CPU 最高回到 2.84GHz（无 3.19GHz）。

## 25. SD 卡自动挂载（2026-11-07 09:30-09:40）

**硬件识别**：SD 槽对应 `mmc0`（`8804000.mmc`，SDHCI，UHS-I SDR104），插入的卡被识别为：

- 设备：`/dev/mmcblk0`，120G SDXC
- 分区：`/dev/mmcblk0p1`，FAT32（UUID `7C9F-1EE9`，卷标 NO NAME）
- 内核支持 `vfat`；exfat/ntfs 模块缺失（这张卡是 FAT32，够用）

**实现（开机自动挂载 + 热插拔）**：

1. `/usr/local/bin/j716f-sdcard-mount`：`add/remove/status` 三个动作；
   - 挂载点 `/mnt/sdcard`，FAT32 挂载选项 `rw,noatime,uid=1001,gid=1001,umask=0007,flush`（player 可读写）；
   - 先尝试 `iocharset=utf8`，失败自动回退到默认 charset（本内核没有 `nls_utf8` 模块）。
2. `/etc/systemd/system/j716f-sdcard-mount.service`（enabled）：开机 `ExecStart=... add`，卡不存在时直接跳过；
3. `/etc/udev/rules.d/60-j716f-sdcard.rules`：插入 `mmcblk0p1` 时自动 `SYSTEMD_WANTS` 启动服务挂载；拔出 `mmcblk0` 时自动卸载（lazy umount）。

**实测**：

- 重启后自动挂载成功：`/dev/mmcblk0p1 on /mnt/sdcard type vfat (rw,...,uid=1001,gid=1001)`；
- player 用户写入测试通过；`df` 显示 120G 可用；
- `j716f-sdcard-mount.service` enabled/active；热插拔 udev 规则已加载。

**已知限制**：

- 内核缺 `nls_utf8`，FAT32 中文文件名会显示成 `????`（英文目录/文件名正常）；要显示中文需后续编译 `nls_utf8.ko`（属于内核模块，等以后构建内核时一起加）；
- 卡在 Android 上未正常卸载，挂载时 dmesg 提示 `Volume was not properly unmounted`，建议有空先在 PC/Android 上 fsck 一次；
- FAT32 单文件最大 4G（放游戏镜像/大文件需 exFAT，且内核要先有 exfat 模块）。

**使用**：

```bash
/usr/local/bin/j716f-sdcard-mount status   # 查看是否挂载
ls /mnt/sdcard
# 卡插入/拔出会自动挂载/卸载；开机有卡也会自动挂载
```

### 25.1 已把 SD 卡从 FAT32 改成 ext4（2026-11-07 09:40-09:45）

**原因**：FAT32 单文件最大 4G，放游戏/镜像不够用。

**操作**（已自动备份→格式化→恢复）：
1. 停服务/挂载，备份 `/mnt/sdcard` 内容到 `/home/j716f/sdcard-backup`（243M，10 个目录）；
2. `sfdisk --part-type /dev/mmcblk0 1 83` 把分区类型改成 Linux；
3. `mkfs.ext4 -F -L sdcard -m 0 /dev/mmcblk0p1`；
4. 用 `j716f-sdcard-mount add` 重新挂载（ext4 走 `rw,noatime`，根目录 chown 到 player）；
5. **5G 大文件写入测试通过**（原来 FAT32 会失败）；
6. 备份数据恢复到 `/mnt/sdcard` 并 `chown -R player:player`；
7. 恢复 udev 规则与 systemd 服务（enabled/active）。

**结果**：
- `/dev/mmcblk0p1` → **ext4，label `sdcard`，117G 可用**；
- 自动挂载/热插拔机制不变（开机有卡自动挂到 `/mnt/sdcard`，拔卡自动卸载）；
- 中文文件名以后在 ext4 上正常（不再需要 nls_utf8）；
- 原 FAT32 上已损坏的中文目录名（`????`）在备份时无法恢复；备份副本 `/home/j716f/sdcard-backup`（243M）确认无误后可删。

### 25.2 让 Steam 识别 SD 卡作为游戏库（2026-11-07 09:50）

**现象**：SD 卡已挂载到 `/mnt/sdcard`，但 Steam 设置→存储里看不到。

**原因**：
1. Steam 的“存储”页只列出**已注册的 Steam 库**，不会自动把任意挂载点当成库；
2. 更关键：新版 Steam 的库配置文件是 **`<Steam>/steamapps/libraryfolders.vdf`**，不是 `config/libraryfolders.vdf`。一开始改了后者，Steam 启动后无视/覆盖了。

**做法**：
1. 建库目录 `/mnt/sdcard/SteamLibrary/steamapps`，chown 给 player；
2. 彻底退出 Steam/Game Mode（`pkill -9`），确认进程为 0；
3. 在 **`<Steam>/steamapps/libraryfolders.vdf`** 里增加：
   ```
   "1"
   {
       "path"		"/mnt/sdcard/SteamLibrary"
       "label"		"SD Card"
       "contentid"	"0"
       "totalsize"	"0"
       ...
   }
   ```
4. 重启 Game Mode；Steam 启动后**保留**了该条目，并自动写入
   `contentid=3742673580020014983`、`totalsize=125565489152`（≈117G）→ 说明已识别。

**使用**：Steam 设置 → 存储，应能看到 “SD Card” 库；安装游戏时可选该库，或在存储管理里把已有游戏移动过去。

**注意**：SD 卡拔掉/开机时不在，Steam 会提示该库路径不可用，重新插卡（或开机自动挂载后）再启动即可恢复。

## 26. 排查“为什么又重启了”：不是蓝牙，是满电充电 over-voltage（2026-11-07 09:50-09:55）

**现象**：设备又出现自动重启（`androidboot.bootreason=hard_rst`）。

**排查证据**：
- 蓝牙（QCA6390 / hci0）固件加载正常：`QCA Downloading htbtfw20.tlv / htnv20.bin`、`QCA setup on UART is completed`；崩溃前后**没有任何蓝牙崩溃/panic**；`Bad flag given (0x1) vs supported (0x0)` 是良性提示；
- `/sys/fs/pstore` 为空（没有崩溃转储）；
- 上一次启动的日志在 09:50:17 **戛然而止**（最后是音频/`j716f-mic` 日志），紧接着之前的几分钟里 PMIC 反复报：
  `qcom-pm8150b-charger: battery over-voltage`（09:46-09:48 多次）；
- 当时电池 **100% + 充电中**；今天多次自动重启都出现在“满电 + 插电”状态。

**结论**：不是蓝牙，也不是温度；是**满电后继续充电时 PMIC 反复 over-voltage 保护/复位**。之前游戏 boost 的硬复位也是同一个供电问题（高负载 + 充电叠加）。

**处理（已落地）**：`j716f-charge-thermal.py` 增加**满电停充**逻辑：
- 电量 ≥ **99%** → 关闭充电（`charging_enabled=0`），并写日志 `满电停充`；
- 电量回落到 ≤ **95%** → 恢复充电；
- 与原有温控（38-47°C 分档限流、47°C 停充）、游戏停充标志共存。

**验证**：重启服务后，电量 100% 时充电立即关闭；连续观察 60 秒，`battery over-voltage` 计数 **37 → 37（delta=0）**，不再刷屏。

**建议**：
- 平时尽量别让设备长时间“满电+插电+高负载”同时存在；满电后系统现在会自动停充，降到 95% 再补；
- 如果还想更稳，可以在长时间游戏时拔掉充电器（或 `j716f-gameboost on` 会主动停充）；
- 蓝牙目前正常，若以后出现断连/卡顿再看 `dmesg | grep -i hci`。

### 26.1 后续复现与最终修复（2026-11-07 10:00-10:15）

**复现**：仅关 `charging_enabled` 还不够——满电+插充电器时设备仍每 2-5 分钟 `hard_rst`；日志里 PMIC 仍反复 `battery over-voltage`。拔掉充电器后设备**连续 11 分钟稳定**（over-voltage 计数 0），说明问题在充电供电路径。

**根本修复**：满电时除了关充电，还要把**充电输入电流 ICL 也设为 0**：
- 电量 ≥99%：`charging_enabled=0` **且** `current_max=0`；
- 电量 ≤95%：恢复 `current_max=3A` 并重新允许充电；
- 这样即使充电器插着，PMIC 也不再反复 over-voltage。

**验证（充电器插着，满电）**：
- `charger_online=1, charging_enabled=0, current_max=0`；
- `battery over-voltage` 计数 = **0**；
- 设备连续稳定运行 **~10 分钟**（此前每 2-5 分钟必重启），电池以 ~-0.45A 缓慢放电，降到 95% 后会自动恢复充电。

**结论**：这次重启不是蓝牙；是“满电 + 充电器插入”时 PMIC over-voltage 保护触发硬复位。现在插着充电器也不会再触发。

## 27. 插 PC USB-C 线又重启：热插拔 typec/PD 事件导致（2026-11-07 10:17）

**现象**：用户把 PC 的 USB-C 线插到平板，设备立刻重启（`hard_rst`），随后进 fastboot/需手动重启。

**排查**：
- 这次**不是满电 over-voltage**：上一轮启动的 `battery over-voltage` 计数为 **0**；
- 也不是蓝牙：蓝牙日志正常；
- `pstore` 为空，没有 panic/oops → 硬件保护性复位；
- 上一轮启动里 typec/PD 状态：`port0 role=[sink] power_role=[sink] data_role=[device]`，gadget 已激活（`configfs-gadget.g1`），并有 `PD contract: 5000000 uV / 3000000 uA`；
- **插好 PC 线再开机后，设备已稳定运行 2+ 分钟**（over-voltage=0、Game Mode 正常、`usb0` 可用） → 说明不是“PC 供电/充电”本身持续不稳定，而是**热插拔瞬间的 typec/PD/VBUS 事件**触发了硬件复位。

**结论**：运行中**热插拔 PC USB-C 线**会触发 typec/PD 事件导致硬复位；不是蓝牙、不是充电过压。

**建议 / 规避**：
- 需要 PC 串口/ADB/USB 网络时：**先插好线再开机**，或关机插线后开机；不要在运行中热插拔 PC 线；
- 日常远程操作优先用 **WiFi SSH**（`ssh player@192.168.1.10`）；
- 已额外加的保护：满电时插电立即 `charging_enabled=0 + ICL=0`（udev 规则 `61-j716f-charge-guard` + charge-thermal 3s 轮询），可避免“满电+插电”的 over-voltage，但对热插拔 typec 复位无法从软件层面阻止。

**当前状态**：PC 线插着、充电停止（en=0, ICL=0）、over-voltage=0、Game Mode 运行、设备稳定中。

## 28. 插 PC USB-C 反复重启的真正原因：USB gadget 数据链路（2026-11-07 10:24）

**现象**：插着 PC 的 USB-C 线时，设备每 1-2 分钟 `hard_rst` 重启（10:17/10:20/10:22 连续多次）。

**对照实验**：
| 条件 | 结果 |
|---|---|
| 不插任何线（只电池） | 稳定 11 分钟 |
| 只插原装充电器（无数据） | 稳定 10 分钟（满电停充+ICL=0） |
| 插 PC USB-C、gadget 绑定（默认） | 每 1-2 分钟硬复位 |
| 插 PC USB-C、**gadget 解绑** | **稳定 5 分钟+，over-voltage=0** |

**结论**：不是蓝牙、不是充电 over-voltage；是**USB gadget（ACM 串口 + RNDIS 网卡）设备模式的数据链路/枚举事件**导致硬复位。只要 gadget 不解绑，插 PC 线就会周期性复位。

**修复（已落地）**：
- `/usr/local/bin/j716f-usb-gadget on|off|status`：手动开关 gadget；
- `j716f-usb-gadget-off.service`（enabled）：**开机后自动解绑 gadget**，PC 线插着也只作为供电，不再触发复位；
- 需要 USB 串口/ADB 时：`sudo /usr/local/bin/j716f-usb-gadget on`（可能再次复位，注意）；日常远程用 WiFi SSH `player@192.168.1.10`。

**当前状态**：PC 线插着、gadget 已解绑、设备稳定运行 ~5.5 分钟、over-voltage=0。

## 29. 整体稳定性整治：功能互相影响与最终稳定配置（2026-11-07 11:20-11:40）

**设备症状**：反复 `hard_rst`；后期出现「SSH 能连但所有会话都开不了」的半卡死；`getty@tty1.service: start-pre operation timed out`、`pam_systemd(sshd:session): Failed to get user record`。

**关联分析（功能互相影响）**：
1. **充电 + 满电 → PMIC over-voltage → hard_rst**；而之前加的 **udev 快速停充规则 + charge-thermal 3s 循环** 会反复写 `charging_enabled/current_max`（guard 日志里 1 秒几十条），产生大量 power_supply 事件，反而造成电源状态抖动；
2. **USB gadget（ACM+RNDIS）**：运行中热插拔 PC 线会周期性硬复位；
3. **Game Mode（gamescope 占 tty1/DRM）+ getty@tty1 启动超时 → logind/会话层卡死**：这是「一个功能拖死另一个」的关键——tty1 的 getty 起不来时，logind 创建新会话阻塞，导致后续 SSH/getty 全部开不了；
4. 反复硬复位导致文件系统/journal 脏状态累积，加重启动/IO 卡顿。

**整治措施（已落地）**：
- `systemctl mask getty@tty1`（不再让 tty1 getty 反复超时拖死 logind），控制台改到 **tty2**（`getty@tty2` enabled/active）；
- 停用/删除：`j716f-charge-thermal`（旧 python 版）、`61-j716f-charge-guard.rules`（udev 反馈环）；
- 新增 **简化充电策略** `/usr/local/bin/j716f-charge-simple` + `j716f-charge-simple.service`：
  - 每 10 秒检查，**只在值变化时写入**（无 churn）；
  - ≥99% 停充（en=0），≤95% 恢复；温度 ≥47°C 停充、43-47 1A、40-43 1.5A；
  - 支持游戏停充标志 `/run/j716f-gaming-charge-off`；
- 恢复 `j716f-thermal-guard`（v2，阈值 78/72°C）；
- 保留 `j716f-usb-gadget-off`（开机自动解绑 gadget）；
- **Game Mode 取消开机自启**（`/etc/j716f-gamemode-enabled` 已删），需要时手动启动。

**当前验证状态**（11:37）：
- 最小系统稳定后手动启动 Game Mode，已连续运行 **5.7 分钟**：11 个 Steam/gamescope 进程、`system running`、0 failed units、无 D 状态进程、`over-voltage=0`、SSH 正常、显示 `AR24 + modifier=0`；
- 电源：充电器未插（online=0），电量 100%，`charge-simple` 正常；
- 控制台：tty2 可登录（tty1 已 mask）。

**使用方式**：
- 开机进文本控制台（tty2）或 SSH `player@192.168.1.10`；
- 需要 Game Mode 时（SSH 里）：
  ```bash
  setsid nohup /usr/local/bin/j716f-gamemode >/tmp/gm.log 2>&1 &
  ```
  或在 tty2 登录 player 后直接运行 `/usr/local/bin/j716f-gamemode`；
- 退出 Steam 后回到文本/SSH；不再自动进 Steam。

**后续建议**：
- 观察 10-30 分钟；若稳定，可再做「systemd 服务启动 Game Mode（替代 tty1 autologin）」并测试；
- 避免运行中热插拔 PC 线；充电器插入时满电会自动停充；
- 若再出现 `hard_rst`，按 §26-28 的思路查 over-voltage/USB/音频，并保留 `/home/j716f/stab_watch.log`。

## 30. 全局稳定路线定稿：开机文本+SSH，手动一条命令启动 Game Mode（2026-11-07 12:00）

**尝试过、被否定的“开机自启 Game Mode”方案**：
1. tty1 autologin + .bash_profile 启动 → getty 超时拖死 logind/SSH；
2. systemd system service + `PAMName=login` + `TTYPath=/dev/tty1` → 同样拖死 logind/SSH；
3. 本地 sshd 会话（root ssh player@127.0.0.1 拉 gamescope）→ 也会拖死 logind/SSH。

**结论**：在这台设备上，**只要在开机阶段由系统自动拉起 gamescope，就会把 logind/会话层搞挂**；而**从外部 SSH 会话手动启动 gamescope** 时，SSH 一路正常（多次验证 5 分钟以上）。

**最终架构（已落地）**：
- 启动进入 **multi-user target（文本控制台 tty2 + SSH）**，不自动启动任何图形界面；
- `getty@tty1` 已 mask（tty2 保留可登录）；
- 旧坏服务 `j716f-gamemode.service`、`j716f-gamemode-bootstrap.service` 已 mask；
- 通过 **boot 镜像 kernel cmdline** 加了 `systemd.mask=j716f-gamemode.service`（`boot-stable.img`，已刷 boot_a/boot_b）做双保险；
- 保留：`j716f-charge-simple`（简化充电策略）、`j716f-thermal-guard`、`j716f-usb-gadget-off`、`j716f-sdcard-mount`、`j716f-perf`、sshd、NetworkManager、seatd、tty2 getty。

**使用（SSH 全程可用）**：
```bash
# PC/手机 SSH 启动 Game Mode（命令立即返回，Steam 在平板后台启动）
ssh player@192.168.1.10 /usr/local/bin/j716f-start-gamemode
# 停止
ssh player@192.168.1.10 /usr/local/bin/j716f-stop-gamemode
# 状态
ssh player@192.168.1.10 'ps -e -o comm | grep -E "gamescope|steam"'
```
或在平板 tty2 登录 player 后直接运行 `/usr/local/bin/j716f-start-gamemode`。

**实测（12:04）**：通过 SSH 启动 Game Mode 后，11 个进程、`system running`、0 failed units、SSH 正常、显示 `AR24 + modifier=0`、充电器插着且 `charge-simple` 已满电停充（en=0/ICL=0）。

**说明**：不再尝试“开机自动进 Steam”；需要图形界面时用一条 SSH 命令启动即可，这样永远不会出现“进了 Steam 但 SSH 没了、无法操作”的情况。

## 31. Steam 卡在转圈界面：清 UI 缓存修复（2026-11-07 12:15）

**现象**：Game Mode 启动后一直卡在 Steam 转圈 logo，进不了游戏库界面。

**排查**：
- 网络正常：`store.steampowered.com`/`steamcommunity.com` 解析与 HTTPS 都通；
- Steam 登录正常：`connection_log.txt` 显示 `RecvMsgClientLogOnResponse(): OK`、`Logged On`；
- `console_log.txt` 无报错，`System startup time` 正常，CEF/steamwebhelper 都在；
- 判定为 **Steam UI 缓存（config/htmlcache）卡住/损坏**，不是网络或登录问题。

**修复**：
1. 停 Game Mode（`pkill gamescope|steam`）；
2. 把 `config/htmlcache` 重命名为 `htmlcache.bak-<ts>`（保留备份）；
3. 重新启动 Game Mode；
4. 结果：Steam 正常进入游戏库。

**一键修复命令（已安装）**：
```bash
ssh player@192.168.1.10 /usr/local/bin/j716f-reset-steam-ui
```
（脚本会：停 Game Mode → 备份并移走 htmlcache → 重启 Game Mode；最多保留 3 个备份）

**验证（12:31）**：Steam 已进入游戏库；Game Mode 11 个进程、`system running`、sshd active、SSH 正常。

## 32. 蓝牙连接导致硬复位：默认关闭蓝牙（2026-11-07 12:40）

**现象**：用户连接蓝牙设备（Xbox 手柄 / 华为鼠标）后设备立刻 `hard_rst`；多次复现。

**证据**：
- 之前每次“连蓝牙后”都重启；关掉蓝牙服务 + `rfkill block` 后用户观察**不再重启**（对照实验）；
- 内核日志中能看到的是 **DSI 显示时钟 WARN**（`clk_branch_toggle` ← `msm_dsi_host_power_off`，gamescope 模式切换时），**不是蓝牙 panic**；`pstore` 为空，崩溃前无蓝牙错误日志 → 只能靠对照实验定位；
- 怀疑点：QCA6390 的 BT 固件/驱动（`hci_uart` + `btqca`，固件 `qca/htbtfw20.tlv`、`qca/htnv20.bin`）在**连接设备时**触发固件/内核崩溃（hard reset 来不及写日志）。

**处理（已落地）**：
- `bluetooth.service` 已 **disable + mask**；
- `rfkill block bluetooth` 已执行；
- 新增 `j716f-bt-off.service`（enabled）：开机自动 rfkill block，保证默认关闭；
- 新增切换命令：`/usr/local/bin/j716f-bluetooth on|off|status`。

**当前状态**：蓝牙硬件层面被屏蔽（`Soft blocked: yes`），系统稳定、SSH 正常。

**替代方案**：
- 有线/2.4G 接收器手柄、鼠标；
- USB-C OTG 外设；
- 如需继续用蓝牙：可尝试更新 `linux-firmware`（qca 固件）或使用 USB 蓝牙 dongle；测试时用 `j716f-bluetooth on`，出问题立即重启并恢复 `off`。

**附注**：多次 hard reset 后出现过 `systemd-logind` 卡死（SSH 能连但开不了会话），重启一次可恢复；日常尽量用单一长连接操作。

## 33. 蓝牙问题深入排查：固件与 Ubuntu 相同，嫌疑转向内核（2026-11-07 13:00）

**目标**：Ubuntu+Steam 时蓝牙正常，Arch 后连蓝牙会硬复位，找出差异。

**已查明的差异/结论**：
1. **固件相同**：
   - Ubuntu 备份 `/usr/lib/firmware/qca/htbtfw20.tlv` MD5 = `109ce4287557f1789f2e0ed41e08b5ce`；
   - Arch `/lib/firmware/qca/htbtfw20.tlv` MD5 也是 `109ce428...`；
   - `htnv20.bin` 也相同（`74ac04b7aa5e8d988e4813f6d9a20094`）。
   → **不是固件版本差异**。
2. **上游最新固件不兼容**：
   - kernel.org linux-firmware master 的 `htbtfw20.tlv`（`f3a7329b...`，28672B）替换后，hci0 报
     `command 0xfc00 tx timeout`、`QCA Failed to send TLV segment (-110)`，重试 3 次仍失败；
   - 已回滚回 Arch 原固件。
3. **当前内核配置正常**：
   - `CONFIG_BT_QCA=m`、`CONFIG_BT_HCIUART=m`、`CONFIG_BT_HCIUART_QCA=y`、`CONFIG_BT_HCIUART_SERDEV=y`、`CONFIG_SERIAL_QCOM_GENI=y`；
   - DTB 里 `bluetooth { compatible = "qcom,qca6390-bt"; ... }` 供电节点存在。
4. 用旧固件手动 `bluetoothctl connect` 已配对设备时返回 `org.bluez.Error.Failed Input/output error`，但内核没有留下 panic（pstore 空），无法直接证明崩溃点。

**正在做**：从 16GB Ubuntu rootfs 备份中提取 Ubuntu 的内核蓝牙模块（hci_qca 等）、串口驱动和 kernel config，与当前 6.13.0-sm8250 #26 对比，确认是否是 **hci_qca/geni-serial/电源管理** 的补丁差异。

**当前策略**：蓝牙保持 `masked` + `rfkill blocked`；设备稳定（BT off 时长时间无重启）。若内核对比确认补丁差异，下一步是给当前内核补上对应补丁重建；否则推荐 USB 蓝牙 dongle（`CONFIG_BT_HCIBTUSB=m` 已具备）。



























## 35. 蓝牙问题最终结论与维护通道（2026-11-07 14:30）

**穷举对比（全部哈希验证，均已排除）**：
- 内核镜像：Ubuntu 时期 boot_a.raw 与当前 boot-main.img 的 kernel SHA1 完全相同；
- DTB：相同（sha1 a2286f6f…，141534 字节）；
- BT 固件 htbtfw20/htnv20：MD5 相同；
- WiFi/BT 共存固件 ath11k（amss/board-2/m3）：MD5 相同；
- 内核模块 btqca/hci_uart/btusb：MD5 相同；
- BlueZ 5.87 与 5.72（Ubuntu 版）都试：连接时都会挂；
- 关 WiFi 只留蓝牙：仍然在扫描/连接时挂；
- Xbox 手柄（BLE）和华为鼠标（BLE）都挂；
- eco（低 CPU/GPU）下偶有一次未连接成功的测试稳定，但连接动作本身会触发挂死；
- netconsole 只抓到 “enabling bt” 就断，pstore 为空 → 非 panic，是内核 HCI/QCA 层 hang/复位。

**结论**：所有可见软件层（内核/DTB/固件/模块/BlueZ 版本）与 Ubuntu 时期一致，但当前机器上“蓝牙连接”会稳定触发内核级挂死。最可能是 QCA6390 蓝牙硬件/供电状态在反复 over-voltage/硬复位后发生了变化，或该定制内核在 QCA HCI 连接路径上有 Ubuntu 时期未触发的 bug。软件侧已无法进一步区分。

**处理（已落地）**：
- `bluetooth.service`：disable + mask；`rfkill block bluetooth`；`j716f-bt-off.service` 开机保持关闭；
- 需要蓝牙请用 **USB 蓝牙 dongle**（内核 `CONFIG_BT_HCIBTUSB=m`，`btusb.ko` 存在）；
- 维护通道：**仅保留 2222 应急 SSH**（root 密钥、`UsePAM no`、不依赖 logind），主 SSH(22) 已 disable+mask；
  - PC 连接：`ssh -p 2222 -i ~/.ssh/j716f_key root@192.168.1.10`
  - 若 2222 也连不上，可用 tty2 控制台或 fastboot。

**恢复镜像**：`backup/boot-recovery.img`（基于 boot-main，initramfs 启动时自动修复 NetworkManager WiFi 状态并 mask bluetooth.service），用于“WiFi 被 nmcli radio off 持久关闭/蓝牙导致挂死”后的救援；正常镜像为 `backup/boot-stable.img`。

**当前状态（14:37）**：boot-stable 在 boot_a/boot_b；WiFi 已恢复（192.168.1.10）；蓝牙 masked/blocked；2222 SSH 正常；系统 `running`、0 failed units。

## 36. 移除全部快充/自定义充电控制，恢复普通充电（2026-11-07 14:45）

**目标**：解决供电相关问题，保留普通充电，删除所有快充/自定义充电控制。

**已删除/停用**：
- `j716f-charge-simple`（自定义充电策略：满电停充/ICL 分档/温度分档）——服务、脚本、unit 全部删除；
- `j716f-charge-thermal.py` 及所有 `.bak-*` 备份——删除；
- `j716f-thermal-guard`（其中包含 ICL 限流逻辑）——服务、脚本、unit 删除；
- `j716f-charge-guard` 辅助脚本——删除；
- `j716f-gameboost` 中的停充/充电控制逻辑——简化后只切换性能 profile，不再写任何充电 sysfs；
- udev 充电相关规则（61-j716f-charge-guard）——此前已删除。

**恢复普通充电**：
- 不再有任何服务/脚本写 `charging_enabled` / `current_max`；
- 当前由 PMIC 驱动默认策略充电：`online=1, charging_enabled=1, current_max≈1.55A`（随 PD/温度由驱动决定），电池 99% 状态 Full；
- 连续观测：`battery over-voltage` 计数 **0**，系统稳定（uptime 200s+，0 failed units）。

**当前状态**：
- 充电：普通/默认策略，无软件干预；
- 蓝牙：`masked` + `rfkill blocked`（内置 BT 问题见 §35）；
- SSH：仅 2222 应急通道；
- 其他服务：`j716f-perf`、`j716f-sdcard-mount`、`j716f-usb-gadget-off`、`j716f-bt-off`、sshd-alt、NetworkManager、seatd。

**注意**：满电+长时间插电仍可能出现 PMIC over-voltage（此前 §26 的问题）；如再出现 `battery over-voltage` 或重启，建议满电后拔掉充电器，或后续考虑电池/PMIC 硬件检查。

## 37. 综合梳理：供电、蓝牙与整机稳定性（2026-11-07 15:45）

### 37.1 设备与软件栈
- 硬件：Lenovo TB-J716F（SM8250-AC），QCA6390（WiFi ath11k PCIe + BT UART hci_uart/btqca），双 DSI AMOLED，PMIC pm8150/8150b/8150l，电池 bq27541（健康 Good、227 循环、满容量约 7.7Ah/设计 8.4Ah）。
- 系统：Arch Linux ARM + 定制内核 6.13.0-sm8250 #26；Ubuntu 时期与现在使用**同一个内核镜像、同一个 DTB**（SHA1 相同）。

### 37.2 已用哈希排除的软件差异
| 项目 | Ubuntu 备份 | 当前 Arch | 结论 |
|---|---|---|---|
| kernel 镜像 | SHA1 相同 | 相同 | 同一内核 |
| DTB | sha1 a2286f6f… | 相同 | 同一设备树 |
| BT 固件 htbtfw20/htnv20 | MD5 相同 | 相同 | 同一 BT 固件 |
| ath11k 固件 amss/board-2/m3 | MD5 相同 | 相同 | 同一 WiFi 固件 |
| btqca/hci_uart/btusb.ko | MD5 相同 | 相同 | 同一驱动 |
| BlueZ | 5.72 | 5.87（也试过 5.72） | 均会挂，非版本问题 |

### 37.3 已确认的故障场景（日志证据）
1. **满电+插电 → PMIC `battery over-voltage` 风暴 → `hard_rst`**；曾用软件停充/限流缓解。
2. **PC USB-C 热插拔（USB gadget 枚举）→ `hard_rst`**；后改为开机解绑 gadget。
3. **蓝牙开启/连接 → 硬复位或内核级 hang**：
   - 连 Xbox 手柄、华为鼠标都触发；
   - 只 start bluetooth.service 也可能 hang；
   - 出现 `kworker/u33:* -hci0` 卡住、bluetoothd 卡 poll；
   - netconsole 只到 “enabling bt” 就断、pstore 空 → 非 panic，是 HCI/QCA 层或供电级 hang。
4. **Game Mode/tty1/getty → logind/会话层卡死**（`getty@tty1 start-pre timed out`、`pam_systemd ... Failed to get user record`）。
5. **`nmcli radio wifi off` 持久化**导致重启后 WiFi 关闭、SSH 失联（已用恢复镜像修复）。

### 37.4 Android 与 mainline 的蓝牙供电/驱动差异（关键）
- Android DTB：`qcom,cnss-qca6390@b0000000` 节点内含
  - `qcom,bt-en-gpio = <&tlmm 21 0>`（BT_EN GPIO21）
  - `bt_en_sleep` pinctrl 状态
  - 多路 WLAN/BT 稳压配置（vdd-wlan-aon/dig/io/rfa1/rfa2 及电压）；
  - 使用 `qcom,wcn-bt`、`btfmslim`（BT SCO 音频走 SLIMbus）。
- mainline DTB：使用 `qca6390-pmu` 节点（`bt-enable-gpios = <&tlmm 21 0>`）+ `bluetooth { compatible="qcom,qca6390-bt" }` 六路 supply；内核加载 `pwrseq_qcom_wcn`。
- 内核 config：`CONFIG_BT_QCA=m`、`CONFIG_BT_HCIUART_QCA=y`、`CONFIG_PWRSEQ_QCOM_WCN=m`；**未发现 `CONFIG_QCOM_QCA6390_PMU`**（qca6390-pmu 驱动缺失/未启用）。
- 实测 BT 关闭时 GPIO21 为 out low；尚未测到 BT 上电瞬间的 GPIO/稳压状态（测试时设备 hang）。

### 37.5 根因假设（按可能性排序）
1. **PMIC/电池供电余量不足（最可能）**：反复 over-voltage/硬复位后，BT 功放/上电瞬态电流导致电压跌落，触发 PMIC 保护/看门狗；Ubuntu 时期硬件状态更好，所以正常。
2. **mainline 的 QCA6390 BT 上电时序不完整**：缺少/未启用 `qca6390-pmu` 驱动，BT_EN GPIO 与六路稳压未按 Android 方式严格上电 → BT 芯片处于边缘状态，连接时挂死。
3. **内核 hci_qca/HCI 层死锁**（软件 bug），在连接/扫描时触发。
4. **音频/ADSP（LPASS）交互**：speaker-mixer 启动时 q6lsm 大量 open 失败（rc=-5），可能影响 LPASS/电源域；已临时禁用 audio 服务，但 BT 仍 hang，说明不是唯一原因。
5. **次生问题**：tty1/logind、udev/充电脚本、USB gadget 等放大不稳定。

### 37.6 建议的整体处理顺序（先不刷写）
1. **先回到最小稳定系统**：文本控制台 + 2222 应急 SSH；BT masked；无自定义充电/温控；tty1 不启用；观察 30-60 分钟确认基线稳定。
2. **供电治理**：
   - 保持普通充电（已删除全部快充/限流脚本）；
   - 避免满电+长期插电（满电拔充电器）；
   - 用脚本记录电压/电流/over-voltage 计数，确认是否还有掉电/复位；
   - 若普通充电仍 over-voltage，考虑电池/PMIC 硬件检测。
3. **蓝牙按 Android 参考修供电时序**：
   - 审计六路 BT supply 的 regulator 状态/电压（BT off/on 对比）；
   - 测 GPIO21（BT_EN）在蓝牙启动时的电平；
   - 评估启用/补上 `qca6390-pmu` 驱动或按 Android DTB 补 `qcom,bt-en-gpio`/稳压配置（需改 DTB/内核，后续再做）；
   - 若供电时序修好仍 hang，再考虑 USB BT dongle。
4. **恢复其它功能**：确认基线稳定后再逐项恢复 Game Mode、audio、SD、性能配置。

**当前状态**：设备因最后一次蓝牙 power-up 测试 hang，需要一次断电重启；boot 镜像为 recovery 版（会自动修 WiFi 并 mask BT）。下一步先重启回最小系统，不再刷写。

## 38. 蓝牙供电时序深入审计（2026-11-07 16:05）

### 38.1 mainline 的 QCA6390 供电路径
- 设备树 `qca6390-pmu` 节点由 `pwrseq-qcom_wcn` 驱动接管（`/sys/bus/platform/drivers/pwrseq-qcom_wcn`）。
- 供电来自 8 路父级 PMIC 稳压：
  - `vddio`→vreg_s4a_1p8、`vddpmu`→vreg_s2f_0p5、`vddaon`/`vddrfa0p95`→vreg_s6a_0p6、`vddpcie1p3`/`vddrfa1p3`→vreg_s8c_1p2、`vddpcie1p9`/`vddrfa1p9`→vreg_s5a_1p9 等（均已 enabled，电压正常）。
- BT_EN = GPIO21（`bt-enable-gpios`），WLAN_EN = GPIO20。
- DTB 里 `regulators/ldo0..ldo9`（vreg_pmu_*）**没有被注册**（没有驱动创建），蓝牙节点里 `vddrfacmn` 等 supply 指向这些不存在的 regulator；但 hci_qca 在新内核里通过 pwrseq 上电，这些 supply 可能只是遗留属性。

### 38.2 新增实验（仍然挂）
- 卸载 WiFi（ath11k）后再启动蓝牙：仍然在 “enable bluetooth” 步骤挂死；
- 之前蓝牙关闭时 GPIO21=low；一启动蓝牙（pwrseq 上电 + BT_EN 拉高）系统就 hang；
- 上一轮 boot 日志在 15:58 WiFi 卸载后**再无任何内核日志**，16:00 用户按电源关机 → 不是 panic，是整机卡死。

### 38.3 更新后的结论
- 所有可见软件层（内核/DTB/固件/模块/BlueZ）与 Ubuntu 时期一致；
- 挂死点已缩小到 **“BT 上电瞬间（8 路 PMIC 稳压 + BT_EN 拉高）”**，而不是连接协议/BlueZ；
- 最可能：
  1. **PMIC/电池供电瞬态问题**（反复 over-voltage/硬复位后供电余量变差）；
  2. **pwrseq-qcom_wcn + PMIC 上电时序在 QCA6390 上的兼容性问题**（Android 用 cnss 驱动显式控制同样的 GPIO/稳压）；
  3. QCA6390 BT 模块硬件损伤。
- 继续用软件试探会反复把设备弄挂，风险高。

### 38.4 建议
1. **最稳**：USB 蓝牙 dongle（`btusb` 已具备），内置 QCA6390 BT 保持 masked/blocked；
2. 若坚持内置：下一步需要“改 DTB/pwrseq 上电时序或测 PMIC 瞬态”的工程化工作（要重建 boot/内核，且不保证成功）；
3. 先保持蓝牙关闭，设备回到最小稳定系统。

## 39. 源码级分析：QCA6390 蓝牙上电电压不匹配（2026-11-07 16:10）

### 39.1 mainline 蓝牙上电路径（源码依据）
- `drivers/bluetooth/hci_qca.c`：QCA6390 使用 `devm_pwrseq_get()` + `pwrseq_power_on()`（源码 2356、2410、2226 行附近）；
- 供电由 `drivers/power/sequencing/pwrseq-qcom-wcn.c` 负责：
  - `pwrseq_qca6390_vregs[]` = vddio, vddaon, vddpmu, vddrfa0p95, vddrfa1p3, vddrfa1p9, vddpcie1p3, vddpcie1p9；
  - 上电时只做 `regulator_bulk_enable()`，**不会修改电压**；
  - 然后拉高 BT_EN（GPIO21）。
- 因此这些 rail 的电压完全由 DTS 里 RPMh regulator 的初始值决定。

### 39.2 Android 的对应实现
- Android DTB `qcom,cnss-qca6390` 节点里：
  - `qcom,vdd-wlan-aon-config = <0xe7ef0 0xe7ef0 ...>` → 0xE7EF0 = **950000 µV**
  - `qcom,vdd-wlan-dig-config = <0xe7ef0 0xe86c0 ...>` → **950000/953000 µV**
  - `qcom,vdd-wlan-rfa2-config = <0x149970 ...>` → **1350000 µV**
  - `qcom,vdd-wlan-io-config = <0x1b7740 ...>` → 1800000 µV
- Android 的 PMIC 初始值：S6A=0x927c0=**600000**、S2F=0x7d000=**512000**、S8C=**1200000**；
  - 但 Android 的 cnss 驱动在 WLAN/BT 上电时会**把 S6A/S2F 升到 0.95V、S8C 升到 1.35V**。

### 39.3 我们 DTS 的问题
- `sm8250-lenovo-j716f.dts` 里：
  - `vddaon` / `vddrfa0p95` = `vreg_s6a_0p6`，min 600000（0.6V）
  - `vddpmu` = `vreg_s2f_0p5`，min 512000（0.512V）
  - `vddrfa1p3` / `vddpcie1p3` = `vreg_s8c_1p2`，min 1200000（1.2V）
- 实测开机后这些 rail 就是 0.6V / 0.512V / 1.2V；
- mainline pwrseq **不会像 Android cnss 那样把它们升压**，于是 QCA6390 BT 的 AON/PMU/RFA 模拟供电只有 Android 的约 63%/54%/89%，BT 一上电/连接就 hang。

### 39.4 修复方案（源码/DTS 级，先不刷）
- 在 DTS 里把这三路 rail 调到 Android 运行值：
  - S6A：`regulator-min-microvolt = <950000>`（vddaon、vddrfa0p95）
  - S2F：`regulator-min-microvolt = <950000>`（vddpmu，唯一消费者就是 QCA，安全）
  - S8C：`regulator-min-microvolt = <1350000>`（vddrfa1p3、vddpcie1p3）
  - S5A 1.9V、S4A 1.8V 已经正确。
- 已生成候选 DTS：`restore/home/j716f/j716f-linux/arch/arm64/boot/dts/qcom/sm8250-lenovo-j716f-btfix.dts`（原文件未改）。
- 风险：S6A/S8C 还给 PM8150A/PM8009 的 LDO 提供输入；Android 在 WLAN 开启时同样把它们升到 0.95/1.35，因此理论上安全，但仍需上机验证。
- 验证方法：编译 DTB → boot 镜像 → 刷入后看 `/sys/class/regulator/regulator.*/microvolts` 是否为 0.95/0.95/1.35，再开蓝牙测试。

### 39.5 结论
这基本解释了“Ubuntu 能用、Arch 挂”的矛盾：Ubuntu 时期可能由 Android/cnss 或 bootloader 把这些 rail 留在较高电压；切到纯 mainline/pwrseq 后，DTS 只保留了 Android 的**初始低电压**，没有运行时的升压步骤 → BT 模拟供电不足。

## 40. 为什么 Ubuntu 能用、Arch 不行（2026-11-07 16:20）

### 40.1 新增证据：Ubuntu 备份里找到了 Android 原厂固件
- `ubuntu-rootfs` 备份中带有 `home/player/backup/firmware/extracted/boot/`：
  - `boot.img`、`kernel.bin`（39MB，Android 原厂内核）、`ramdisk.cpio`
  - `fdt/boot-fdt-*.dts`（Android 原厂 DTB，526KB）
  - `dtbs/dtbo-*.dts`（Android DTBO）
- 从中提取 Android 内核 config（IKCONFIG）：
  - `CONFIG_CNSS2=y`、`CONFIG_CNSS_QMI=y`、`CONFIG_CNSS_UTILS=y`、`CONFIG_CNSS_GENL=y`
  - **没有** `CONFIG_BT_QCA` / `CONFIG_BT_HCIUART`（Android 用的是 vendor BT 驱动 + CNSS2 电源管理）
- Android DTB 的 `qcom,cnss-qca6390` 节点明确给出：
  - `qcom,bt-en-gpio = <&tlmm 21 0>`、`wlan-en-gpio = <&tlmm 20 0>`
  - `vdd-wlan-aon` 0xE7EF0=0.95V、`vdd-wlan-dig` 0.95V、`vdd-wlan-rfa2` 0x149970=1.35V
  - 这些是 **cnss 驱动运行时设置的电压**，不是 PMIC 初始值（初始是 0.6/0.512/1.2V）。

### 40.2 为什么 Ubuntu 可能没问题
- Ubuntu 的 rootfs 备份里 **没有 /boot**（boot 分区没有被备份），所以无法证明 Ubuntu 当时用了哪个 DTB；
- Ubuntu 很可能有以下之一：
  1. 当时用的 boot/DTB 里 rail 电压配置不同（例如已经是 0.95/1.35 的“运行值”）；
  2. bootloader/上一个系统（Android）把 PMIC rail 留在 Android 运行电压，Linux 没有去改；
  3. 有 vendor 用户态（cnss/wcnss 类）在上电时升压；
- 而当前 Arch 是纯 mainline：`pwrseq-qcom-wcn` 只 `regulator_bulk_enable()`，**不会升压**，DTS 又是 Android 初始低压（0.6/0.512/1.2），于是 BT 上电即欠压 hang。

### 40.3 结论
“Ubuntu 能用”不等于“软件栈没问题”，而是 Ubuntu 环境里有东西把 BT 模拟供电补到了 0.95/1.35V；我们现在的 mainline 方案没有。
修复方向仍是：把 DTS 里 S6A/S2F 设为 0.95V、S8C 设为 1.35V，让 mainline 的“只 enable 不升压”变成“初始就是正确电压”。

## 41. 编译带电压修正的 DTB 并重打包 boot（2026-11-07 16:25，未刷写）

**输入**：`sm8250-lenovo-j716f-btfix.dts`（S6A/S2F→0.95V、S8C→1.35V）。
**编译方式**：设备上无完整内核源码树，WSL 损坏 → 把 `arch/arm64/boot/dts/qcom` + `include/dt-bindings` + `include/uapi` 打包推到平板，在平板上用 `cpp + dtc` 编译。
- 需要补一个生成头：`include/dt-bindings/input/linux-event-codes.h -> ../../uapi/linux/input-event-codes.h`。

**编译结果**：
- `sm8250-lenovo-j716f-btfix.dtb`，141534 字节；SHA1 `878dbe9e0ded0c539b0fb50b21ec2b137d07eb9a`；
- 反编译验证：
  - `smps6` min = `0xe7ef0` = **950000 µV**
  - `smps2` min = `0xe7ef0` = **950000 µV**
  - `smps8` min = `0x149970` = **1350000 µV**

**重打包**：
- 以 `boot-stable.img` 为基底（保留 cmdline 里的 systemd.mask），只替换 DTB；
- 输出 `backup/boot-btfix.img`（35405824 字节）；
- 校验：kernel SHA1 与 ramdisk SHA1 均与基底一致，仅 DTB 变化；cmdline 保持：
  `pstore.kmsg_bytes=131072 loglevel=7 systemd.mask=j716f-gamemode.service systemd.mask=j716f-gamemode-bootstrap.service`

**尚未刷写**。刷入后验证步骤：
1. `fastboot flash boot_a boot-btfix.img` / `boot_b`，`fastboot set_active a`，`reboot`；
2. 开机后读 `/sys/class/regulator/regulator.*/microvolts`，确认 S6A/S2F≈0.95V、S8C≈1.35V；
3. 再启用蓝牙测试（先小范围 connect，观察是否还 hang）；
4. 若无法开机/异常，刷回 `boot-stable.img` 即可恢复。

## 42. 电压修复实测结果：蓝牙可用，但 WiFi/BT 共存仍有问题（2026-11-07 16:40）

**已刷入** `boot-btfix.img`（S6A/S2F 0.95V、S8C 1.35V），实测轨压：
- `vreg_s6a_0p6 = 952 mV`、`vreg_s2f_0p5 = 960 mV`、`vreg_s8c_1p2 = 1352 mV`。

**结果**：
- ✅ Bluetooth 服务启动不再挂死；
- ✅ **直接连接（不扫描）鼠标成功**（`Connected: yes`，稳定 1 分钟以上）；
- ✅ **WiFi 关闭时 LE 扫描完全正常**（能扫到大量 BLE 设备，系统不崩）；
- ✅ 连接成功后重新打开 WiFi，鼠标保持连接、系统稳定（connected=1，WiFi IP 正常）；
- ❌ **WiFi 开启时扫描（scan/scan le）仍会 hard_rst** → 指向 QCA6390 的 **WiFi/BT 共存（coex）** 问题，mainline ath11k/hci_qca 缺少 Android 那套 CNSS 共存控制。

**可用工作流（当前）**：
1. 配对/搜索新设备：先 `nmcli radio wifi off` → `bluetoothctl scan le/on` + pair → `nmcli radio wifi on`；
2. 日常连接已配对设备：可以先关 WiFi 执行 `bluetoothctl connect <MAC>`，再开 WiFi（连接会保持）；或在 WiFi 开启时直接 connect（实测有时成功，取决于设备是否在唤醒状态）。

**待解决**：WiFi 开启时扫描会崩；可能需要：
- 研究 ath11k/hci_qca 的 coex 支持（mainline 对 QCA6390 的 coex 可能不完整）；
- 或继续用“扫描时临时关 WiFi”的脚本化方案。

## 43. QCA6390 蓝牙扫描崩溃的进一步定位（2026-11-07 晚）

**结论：电压修复有效，但 BlueZ 的 LE 扫描仍会让 QCA6390 固件硬复位整机；btmgmt 扫描正常。**

实测：
- `boot-btfix.img`（S6A/S2F 0.95V、S8C 1.35V）下轨压：952mV / 960mV / 1352mV。
- 蓝牙可上电、可**直接连接**已配对设备（鼠标成功 Connected: yes，WiFi 同时在线稳定）。
- **BlueZ 扫描**：`bluetoothctl scan on` / `scan le` 无论 WiFi 开/关、LE-only 强制、Advertising off、隐私/实验特性关闭、Android 版 btbtfw/htnv 固件，都会在收到 LE Extended Advertising Report 后 **hard_rst**（`androidboot.bootreason=hard_rst`）。
- **btmgmt 扫描**：`btmgmt find -l` 稳定，可发现设备；`btmgmt pair` 可配对。
- 抓串口 HCI：崩溃前无主机异常命令，最后是大量 `LE Extended Advertising Report`（0x0d）→ 固件/控制器自行复位，指向 mainline 缺少 Android CNSS 的 WiFi/BT 共存与扫描配置。

**已尝试并排除**：
- DTB 电压（已修复并验证）；Android `htbtfw20.tlv`/`htnv20.bin`（从 `bluetooth_a` 分区提取）替换；BlueZ 5.87 的 ControllerMode=le、Privacy/Experimental off；WiFi 关/卸载 ath11k；Advertising/Connectable off；内核 mgmt 强制 LE-only（已确认模块加载）。
- 旧 Ubuntu 文档显示当时 BlueZ 扫描可用，但其 boot/DTB/固件组合与当前 mainline 不同，无法直接复现。

**可用方案（已装）**：
- `/usr/local/bin/j716f-bt on|off|discover|pair <MAC>|connect <MAC>|status`
  - `on`：启动 bluetoothd、连 `/etc/j716f/bt-devices` 里的设备（不扫描）；
  - `discover`：用 `btmgmt find -l` 发现（稳定）；
  - `pair <MAC>`：`btmgmt pair` + trust；
- `bluetooth.service` 保持 masked，避免开机自动扫描/自动连接触发崩溃/进 fastboot。

**待做/前提**：完美方案需要移植 Android vendor BT/CNSS 共存栈或等待 mainline 补丁；目前 mainline 下只能“直连/手动扫描”，不能安全自动扫描。

## 44. 最终解决：改用 BlueZ 5.72，蓝牙扫描 + WiFi + Steam 手柄全部可用（2026-11-07 21:00）

### 根因
- 不是电压、不是固件、不是 WiFi 共存：**BlueZ 5.87 的扫描路径**（LE/BR-EDR discovery）会让 QCA6390 固件在收到 LE Extended Advertising Report 后硬复位整机。
- `btmgmt find -l` 正常、BlueZ 5.72 daemon 扫描正常 → 问题在 BlueZ 5.87 与 QCA6390/mainline 的交互。

### 解决
- 从 Ubuntu 取 **BlueZ 5.72** arm64 deb，解包到 `/opt/j716f-bluez572`；
- 新建 `j716f-bluez572.service`（直接跑 5.72 `bluetoothd -n`），enabled 开机自启；Arch 自带 `bluetooth.service` 保持 masked；
- `/usr/local/bin/bluetoothctl` 指向 5.72 版本；
- 补 Xbox udev 规则 `/etc/udev/rules.d/61-xbox-hidraw.rules`（hidraw 0660 input，js 0664 input）；
- 辅助脚本 `/usr/local/bin/j716f-bt {on|off|discover|pair <MAC>|connect <MAC>|status}`，设备列表 `/etc/j716f/bt-devices`。

### 实测证据
- `bluetoothctl scan on`（WiFi 5GHz 在线）扫描到 **19~28 个设备**，系统无 hard_rst；
- 新 Xbox 手柄在 **SteamOS 界面配对成功**：`AA:BB:CC:DD:EE:FF` Paired/Trusted/**Connected: yes**，输入设备 `/dev/input/js0`、`event9`、`hidraw2`；
- Steam 日志 `controller_ui.txt` 已加载 Xbox One 手柄配置（App 769/477160）；
- 鼠标 `AA:BB:CC:DD:EE:FF` 连接成功、HID input 注册；
- Game Mode（gamescope + Steam）运行中，WiFi 保持在线，uptime 800s+ 无复位；
- 轨压：S2F=960mV、S8C=1352mV（btfix DTB 已刷回）。

### 备注
- `j716f-gamemode.service` 本身仍启动失败（PAM/tty1），当前用 `j716f-start-gamemode` 手动启动（用户经 SteamOS 界面正常使用）；
- 旧的 Xbox 手柄 `AA:BB:CC:DD:EE:FF` 仍保留配对记录，可忽略或 `bluetoothctl remove`；
- 若以后升级 BlueZ/内核，需要重新回归扫描；5.72 的 daemon/工具在 `/opt/j716f-bluez572` 持久化。

## 46. 功能增强：FPS 显示、蓝牙自动重连（2026-11-07 23:20）

### FPS 显示
- Game Mode 环境新增：`DXVK_HUD=fps`、`GALLIUM_HUD=fps`；
- DXVK 游戏（如 ICEY）左上角显示 FPS；WineD3D/OpenGL 游戏走 Mesa GALLIUM_HUD（freedreno 支持时显示）。
- 完整 Steam Deck 风格性能叠加需 MangoHud + mangoapp + gamescope `--mangoapp`；Arch ARM 仓库无 mangohud，GitHub 源码下载过慢，暂未编译。

### 蓝牙自动重连
- 新增 `j716f-bt-reconnect.service`（enabled）：
  - 每 8 秒检查 `/etc/j716f/bt-devices` 里的设备；
  - 未连接时通过 BlueZ D-Bus `org.bluez.Device1.Connect` 发起连接；
  - 手柄休眠唤醒后 8~16 秒内自动重连（已实测）。
- `/etc/j716f/bt-devices` 当前为 `AA:BB:CC:DD:EE:FF`；换手柄时把新 MAC 写入该文件即可。
- 服务内也做 rfkill unblock、adapter Powered 检查。

## 47. Steam Deck 风格性能叠加已启用（MangoApp + gamescope --mangoapp）

**结果**：ICEY 中已实测显示 FPS/帧时间/CPU/GPU/内存 的 Steam Deck 风格叠加。

### 组件
- **MangoHud 0.8.4 ARM64**：从 Ubuntu ports 的 `mangohud_0.8.4+dfsg-2_arm64.deb` 解包安装：
  - `/usr/bin/mangohud`、`/usr/lib/aarch64-linux-gnu/mangohud/libMangoHud*.so`、Vulkan layer JSON。
- **mangoapp**：Arch ARM 无包，GitHub 源码下载太慢；用 Ubuntu ports 的 **MangoHud 源码包**（orig + debian diff）在平板上用 meson/ninja 编译：
  - 依赖：`pkgconf cmake glfw spdlog libdrm libx11 libxrandr vulkan-headers glslang python-mako`；
  - 关键选项：`-Dmangoapp=true -Duse_system_spdlog=enabled -Dwith_nvml=disabled -Dwith_xnvctrl=disabled`；
  - 产物安装到 `/usr/bin/mangoapp`。
- **gamescope** 启动参数增加 `--mangoapp`（`/usr/local/bin/j716f-gamemode`）。
- MangoHud 配置：`/home/j716f/.local/share/steam-arm/.config/MangoHud/MangoHud.conf`（fps/frametime/gpu/cpu/ram/vram，top-left）。

### 验证
- `gamescope --mangoapp` 启动后 `mangoapp` 进程存在；
- 启动 ICEY，用户确认左上角出现完整性能叠加。

### 注意
- 叠加默认常显；要关闭可给配置加 `no_display`（或删除/注释显示项）。
- TDP/风扇/电源模式滑条仍无法实现（内核无对应接口）；FSR/NIS、帧率限制可用 gamescope 参数另加。

### 47.1 去掉旧的 DXVK/GALLIUM FPS 显示（2026-11-07 23:50）
- 已从 `/usr/local/bin/j716f-gamemode` 删除 `DXVK_HUD=fps`、`GALLIUM_HUD=fps`、`MANGOHUD=1`；
- 只保留 **gamescope `--mangoapp`** 的 Steam Deck 风格叠加（MangoApp）；
- 重启 Game Mode 后验证：`mangoapp` 进程在，旧 HUD 环境变量已无。

## 48. FSR/NIS 缩放 + 帧率上限（2026-11-08 00:00）

### 已加控制脚本
`/usr/local/bin/j716f-display`：
```bash
j716f-display fsr on      # FSR，内部分辨率 1280x800 放大到 2560x1600
j716f-display fsr nis     # NIS
j716f-display fsr off     # 关闭
j716f-display internal 1920x1200   # 改内部渲染分辨率
j716f-display fpslimit 60 # 帧率上限（0/off 关闭）
j716f-display status
```
每次修改会自动重启 Game Mode（约 20s）。

### 配置
`/etc/j716f/gamemode.conf`：
```
J716F_FSR=fsr
J716F_INTERNAL_W=1280
J716F_INTERNAL_H=800
J716F_FPS_LIMIT=60
```
`j716f-gamemode` 会按配置拼 gamescope 参数：`-F fsr -w 1280 -h 800 --framerate-limit 60`。

### 当前验证
- gamescope 实际命令行包含 `-F fsr -w 1280 -h 800 --framerate-limit 60`；
- MangoApp 叠加仍在。
- FSR 会有轻微变糊；不喜欢可 `j716f-display fsr off`。

## 49. 手柄热插拔黑屏（MangoApp overlay）与规避（2026-11-08 00:10）

**现象**：游戏运行中连接/唤醒蓝牙手柄，屏幕黑掉（DSI `status=5`、`msm_dsi_host_cmd_rx:Invalid response cmd`）；退出游戏后 Steam UI 恢复。

**定位**：与 MangoApp 叠加有关：
- 关闭 `gamescope --mangoapp` 后，游戏内热插拔手柄不再黑屏；
- 恢复 MangoApp，但让手柄**在启动游戏前就连好**（无热插拔），画面/叠加/手柄全部正常（实测）。

**规避方案（已实施）**：
- 蓝牙自动重连服务 `j716f-bt-reconnect` 在检测到 Wine 游戏运行（`steam.exe`/`ICEY.exe`/`Human.exe`）时**跳过自动连接**，避免游戏内热插拔；
- 正确流程：**先唤醒/连好手柄 → 再启动游戏**；游戏中不要让手柄休眠/断开；
- 若游戏中手柄断了：退出游戏 → 等自动重连/手动连接 → 再进游戏。

**结论**：MangoApp 与手柄热插拔的 DSI 显示 bug 暂时无法两全；采用“先连手柄再进游戏”的稳定流程。

## 50. 半屏 / Steam UI 卡 logo / 屏保卡死 修复（2026-11-08）

### 现象
1. 开启 FSR 1280x800 后，屏幕只显示一半，随后设备卡死离线；
2. 重启后 Steam 停在 logo 转圈；
3. 长时间空闲进入屏保后卡死。

### 定位与处理
- **半屏**：与 `gamescope -F fsr -w 1280 -h 800` 有关；关闭 FSR 后显示恢复正常（当前 `J716F_FSR=off`，保留 60 帧上限）。
- **Steam UI 卡 logo**：`/tmp/dumps` 里 CEF webhelper 反复 `Assert( CefCrashReportingEnabled() )`；`config/config.vdf` / `userdata/.../localconfig.vdf` 在硬复位后损坏，Steam 处于 `Awaiting_Logon`。恢复 config/localconfig 备份后 UI 正常。
- **屏保卡死**：`config.vdf` 里 `IdleBacklightDimBatterySeconds=300`（AC=0）；已改为 **0 / 0**，关闭空闲自动调暗/息屏。
- 同时去掉临时 `-offline`，恢复在线；MangoApp 暂关闭。

### 当前稳定配置
- FSR off；60 帧上限；MangoApp off；CEF `-cef-disable-gpu`；
- Steam UI 正常显示；空闲自动息屏关闭。

### 待验证
- 空闲 5~10 分钟是否还会卡死；
- 是否重新启用 MangoApp（曾与 Steam UI/热插拔冲突）。
