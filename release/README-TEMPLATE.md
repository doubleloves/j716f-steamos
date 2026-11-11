# J716F 发布版 · 安装说明（模板）

> 适用设备：**Lenovo Xiaoxin Pad Pro 2021（TB-J716F，骁龙 870 / 8G+128G）**
> 刷机有风险（变砖、丢保修、数据全清），请先读完再操作。仅限同型号。

## 0. 包里有什么

```
j716f-arch-steam-v<ver>/
├─ README.md                # 本文件
├─ SHA256SUMS               # 所有镜像的校验和
├─ install.ps1              # Windows 一键线刷
├─ tools/platform-tools/    # fastboot.exe
└─ images/
   ├─ boot.img              # 启动镜像（kernel + initramfs + DTB）
   └─ rootfs.simg           # 根文件系统（Android sparse ext4）
```

## 1. 前提

1. 平板是 **TB-J716F**（不是别的联想平板/手机）。
2. bootloader **已解锁**（开机有 orange state 提示）。
3. Windows 10/11 电脑 + USB-C 数据线；安装好 fastboot 驱动。
4. 平板电量 > 50%。

## 2. 线刷（首次安装）

1. 解压整个包到电脑（路径不要有中文/空格）。
2. 平板关机，按住 **音量加 + 电源** 进入 fastboot（或用 `adb reboot bootloader`）。
3. 插 USB 线，确认 `fastboot devices` 能看到设备。
4. 在包目录运行：
   ```
   powershell -ExecutionPolicy Bypass -File install.ps1
   ```
   看到提示后输入 `YES` 确认（**会清空平板全部数据**）。
5. 等待 10–30 分钟（不要拔线），自动重启。
6. 首次开机 1–2 分钟；完成后需要配网。

## 3. 首次开机 / 配网

> 发布版**不含 Steam 客户端**，所以首次开机进入的是**设置控制台**（tty1 自动登录占位用户，不是 Game Mode）。
> 设置流程：自定义登录名/密码 → 重启 → 配网 → 装 Steam → 重启进 Steam。

- **第一次进系统会要求自定义用户名和密码**（占位用户 `player`，默认密码 `j716f`；家目录固定 `/home/j716f`，不影响使用）。
- 设好用户名/密码后 `sudo reboot`。
- 配网任选一种：
  1. 接键盘保护套/U 盘键盘，`Ctrl+Alt+F2` 进 tty2，运行 `nmtui`；
  2. SD 卡根目录建 `/j716f/wifi.txt`（内容 `SSID=你的WiFi`、`PSK=密码`），首次启动会自动连接；
  3. 有 TWRP/串口等高级环境时自行配置。
- 然后安装 Steam（见下），`sudo reboot` 后进入 Game Mode。
- Steam：本发布版**不包含** Valve 客户端（合规考虑），需要你自己安装：
  - 方式 1（自动，推荐）：先按上面配好网，然后执行
    ```
    sudo j716f-install-steam.sh
    ```
    它会在**你的设备上**从 Valve 官方 CDN / Steam-ARM 社区安装器下载客户端；
  - 方式 2（手动）：
    ```
    git clone https://github.com/Scrumpper/Steam-ARM
    cd Steam-ARM
    sudo ./install.sh
    ```
  - 安装完成后运行 `sudo reboot`，之后 Game Mode 会自动进入 Steam。
  - 登录使用**你自己的 Steam 账号**（本镜像不含任何账号数据）。

## 4. 已知问题

- 麦克风/相机：见 release 说明（按当前基线）。
- CPU/GPU 超频项默认关闭，保持稳定基线。
- `qbootctl` 服务会每启动标记槽位成功，**不要用 `qbootctl -s` 切槽**。
- 连续重启不会再进 fastboot（7 次计数问题已解决）。
- 若系统起不来：用 mini-recovery（见下）或重新线刷。

## 5. 卡刷（更新/重装，不需要电脑）

前提：设备已安装 mini-recovery（本发布版已包含）。

1. 把 `rootfs.img.gz`、`boot.img`、`SHA256SUMS` 放到 SD 卡 `/j716f/`；
2. 平板系统内执行：
   ```
   sudo j716f-reboot-recovery
   ```
3. 自动重启进 recovery → 校验 → 刷写 → 重启回系统。

## 6. 回退 / 救砖

- 线刷包里重新跑 `install.ps1`（会清数据）。
- 有原厂 Android 固件时可用 Lenovo 官方工具刷回。
- 卡刷恢复：SD 卡放 payload 后进 recovery。

## 7. 校验

```
certutil -hashfile images\boot.img SHA256
certutil -hashfile images\rootfs.simg SHA256
```
与 `SHA256SUMS` 对比。

## 8. 支持

- 项目主页：<填 GitHub/发布页>
- 问题反馈请附：设备型号、系统版本、`install-log.txt`、`journalctl -b` 关键日志。
