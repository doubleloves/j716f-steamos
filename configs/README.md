# configs/ — 系统配置

这些文件来自实机 `/etc/systemd/system`、`/etc/j716f`、`/etc/udev/rules.d`、`/usr/share/alsa/ucm2`。
部署方式见 [docs/87](../docs/87-仓库导读与代码结构.md)。

## systemd 单元（`configs/systemd/system/`）

| 类别 | 单元 | 说明 |
|---|---|---|
| 启动/会话 | `j716f-gamemode.service`、`j716f-gamemode-autostart.service`、`j716f-gamemode-bootstrap.service`(链接)、`getty@tty1.service.d/{autologin,noblank}.conf` | 开机进 Game Mode、tty1 自动登录 |
| 显示/性能 | `j716f-perf.service`、`j716f-cpu-layout.{service,timer}`、`j716f-cpu7-burst.service`、`j716f-gpu-game-mode.{service,timer}`、`j716f-irq-affinity.{service,timer}`、`j716f-idle-fix.service`、`j716f-pageout-steamui.{service,timer}` | 调频/中断/空闲页换出 |
| 电源/热控 | `charge-status.service`(若存在)、`j716f-charge-guard.service`、`j716f-stability.service` | 充电温控/稳定性 |
| 存储 | `j716f-sdcard-mount.service` | microSD 挂载 |
| 输入 | `j716f-volume-keys.service` | 音量键 |
| 蓝牙 | `j716f-bluez572.service`、`j716f-bt-off.service`、`j716f-bt-reconnect.service` | BlueZ 5.72、按需关蓝牙、自动重连 |
| 相机 | `j716f-camera-setup.service`、`j716f-camera-daemon.service`、`j716f-front-camera-daemon.service`、`j716f-wide-camera-daemon.service`、`j716f-microphone.service` | 相机/麦克风相关（麦克风为历史实验） |
| USB | `j716f-usb-gadget-off.service`(链接) | 关闭 gadget |
| 发布/运维 | `j716f-firstboot.service`、`j716f-boot-ok.service`、`j716f-mark-success.service` | 首启、启动成功标记、qbootctl |
| 调试 | `j716f-pstore-tune.service`、`j716f-netconsole.service`（已删除） | pstore / netconsole（含个人 IP，不发布） |

> 发布版应清理调试单元；`j716f-mark-success.service` 是解决 ABL 计数问题的关键，**必须保留**。

## `configs/j716f/`

| 文件 | 内容 |
|---|---|
| `gamemode.conf` | gamescope 参数：FSR/内部分辨率/帧率上限（`J716F_FSR`、`J716F_INTERNAL_W/H`、`J716F_FPS_LIMIT`） |
| `bt-devices` | 需要自动重连的蓝牙设备 MAC 列表（换手柄时改这里） |

## `configs/udev/rules.d/`

| 规则 | 用途 |
|---|---|
| `60-j716f-sdcard.rules` | microSD 挂载/权限 |
| `61-xbox-hidraw.rules` | Xbox 手柄 hidraw 权限 |
| `90-j716f-camera.rules` | 相机设备节点权限 |
| `99-backlight.rules` | 背光权限 |

## `configs/greetd/config.toml`

greetd 配置（tty1）。发布版可能通过 setup console 覆盖/恢复，见 `release/rootfs/j716f-user-setup.sh`。

## `configs/ucm/`

ALSA UCM2 配置，部署到：

```bash
/usr/share/alsa/ucm2/conf.d/sm8250/Lenovo Xiaoxin Pad Pro 2021.conf
/usr/share/alsa/ucm2/Qualcomm/sm8250/J716F-HiFi.conf
```

四扬声器（CS35L41）依赖这些文件；缺失会无声或路由错误。
