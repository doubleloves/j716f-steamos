# scripts/ — 设备端脚本

这些脚本来自实机 `/usr/local/bin`、`/usr/local/sbin`，用于 Game Mode、显示、输入、存储、电源/温控、
蓝牙、相机等。**不是自动安装器**；部署方法见 [docs/87](../docs/87-仓库导读与代码结构.md)。

路径已按发布版统一为 `/home/j716f`；如果家目录不同，请先按 [docs/86](../docs/86-用户名通用化与首启自定义.md) 调整。

## 会话 / Game Mode

| 脚本 | 用途 |
|---|---|
| `j716f-gamemode` | Game Mode 主启动脚本：按 `/etc/j716f/gamemode.conf` 拼 gamescope 参数并启动 Steam |
| `j716f-start-gamemode` / `j716f-stop-gamemode` | 启动/停止 Game Mode（供 SSH/服务调用） |
| `j716f-gamemode-cage` | gamescope 的 cage/kiosk 变体 |
| `game-mode` | 兼容旧入口 |
| `steamos-toggle` / `steamos-session-select` | 在"开机进 Steam/桌面"之间切换（历史逻辑） |
| `j716f-reset-steam-ui` | Steam UI 卡 logo/窗口异常时重启 UI |
| `j716f-pageout-ui` / `j716f-pageout-steamui` | 把 Steam UI 空闲页换出到 zram（省内存） |
| `x-freeze-watchdog.sh` | X/游戏卡死检测与恢复 |
| `display-recover` | 显示异常时恢复（重新 modeset/重启会话） |

## 显示 / 性能

| 脚本 | 用途 |
|---|---|
| `j716f-display` | FSR/NIS、内部分辨率、帧率上限、状态查询（`j716f-display status`） |
| `brightness` | 背光控制封装 |
| `j716f-perf` | 性能档位/调频策略 |
| `j716f-gameboost` | 启动游戏时提升 CPU/GPU 档位 |
| `j716f-gpu-game-mode` | 游戏时 GPU 频率（userspace/固定档） |
| `j716f-cpu-layout` | CPU 大小核布局/亲和 |
| `j716f-cpu7-burst` | CPU7 短时 boost |
| `j716f-irq-affinity` | 中断亲和绑定 |
| `j716f-idle-fix.sh` | 空闲/休眠相关修正 |
| `j716f-stability.sh` | 稳定性巡检（温度/频率/复位记录） |
| `j716f-pstore-tune.sh` | pstore/ramoops 配置（崩溃日志保留） |

## 电源 / 充电 / 热控

| 脚本 | 用途 |
|---|---|
| `j716f-charge-guard` | 充电温控：按温度分档限流，不轻易停充 |
| `j716f-cpu-cap.sh` | CPU 频率/功耗上限设置 |

## 输入 / 外设

| 脚本 | 用途 |
|---|---|
| `j716f-volume-keys` | 音量键映射（含 uinput/udev 配合） |
| `j716f-usb-gadget` | USB gadget（ACM/RNDIS/大容量存储）开关，用于调试/救援 |
| `j716f-sdcard-mount` | microSD 自动挂载/权限处理 |

## 蓝牙

| 脚本 | 用途 |
|---|---|
| `j716f-bluetooth` / `j716f-bt` | 蓝牙状态/开关辅助 |
| `j716f-bt-reconnect` | 按 `/etc/j716f/bt-devices` 自动重连手柄；检测到 Wine 游戏时跳过，避免热插拔黑屏 |

## 相机

| 脚本 | 用途 |
|---|---|
| `j716f-camera-daemon` | 主相机 daemon |
| `j716f-front-daemon` / `j716f-wide-daemon` | 前摄/广角 daemon |

## Steam-ARM 辅助

| 脚本 | 用途 |
|---|---|
| `steam-arm-compatmap` | 生成/维护 Steam 兼容性映射 |
| `steam-arm-icon` | 图标/桌面集成 |
| `steam-arm-remoteplay` | Remote Play 相关 |
| `steam-arm-glx-lax` | GLX 兼容设置 |
| `steam-arm-xpad-dedup` | 重复 xpad 设备去重 |

> 说明：部分脚本是调试/历史产物（如 `j716f-netconsole.sh` 已删除，含个人网络信息）；
> 发布版只保留必要项。
