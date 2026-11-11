# 89 · FAQ 与排障

本文按"现象 → 原因 → 处理"整理；更详细的背景在对应文档里。

---

## 启动 / 槽位

### Q1. 重启几次后开机直接进 FASTBOOT，是不是变砖了？

不是。Qualcomm ABL 的 A/B 重试计数在 **GPT 属性位**里，约 7 次启动没有标记 success 就会把槽标成 unbootable。

- 应急（电脑）：`fastboot set_active a && fastboot reboot`
- 根因/长期修复：每启动执行 `qbootctl -m`（发布版已内置 `j716f-mark-success.service`）
- 详见 [docs/83](83-ABL计数彻底解决.md)

### Q2. 开机黑屏 / 只显示半屏 / 花屏

- 本项目记录过 FSR（`1280x800 → 2560x1600`）导致半屏/卡死；**先完整重启**，不要继续改参数；
- 发布基线为 **FSR 关闭、原生 2560×1600、60 帧上限**；
- 检查是否刷了实验性 DTB（3.19 GHz/GPU 670），回退到稳定基线镜像；
- 相关记录：[docs/71](71-Arch-GameMode-实施记录.md) §50、[docs/75](75-稳定基线.md)

### Q3. 卡在 Lenovo logo

- 长按电源 15–20 秒强制关机再开机；
- 若落到 fastboot：按 Q1 处理；
- 若反复：进 mini-recovery 重刷 boot（`gpt_slot_attrs`/`write_bcb` 进入），或 PC 线刷。

### Q4. 系统时间变成 1970 年

RTC 在启动早期未初始化，联网后 NTP/chrony 会校正；不影响使用。若长期不对，检查 `timedatectl` 与
`systemd-timesyncd`。

---

## Steam / Game Mode

### Q5. Steam UI 卡 logo / 一直转圈 / `Awaiting_Logon`

- 硬复位后 `config/config.vdf`、`userdata/.../localconfig.vdf` 可能损坏；
- 从备份恢复这两个文件（基线里有 Steam config 备份），或删除后重新登录；
- CEF 报 `Assert( CefCrashReportingEnabled() )` 时，尝试 `-cef-disable-gpu`；
- 见 [docs/71](71-Arch-GameMode-实施记录.md) §50。

### Q6. 游戏里热插拔蓝牙手柄 → 黑屏

- 已知 MangoApp overlay 与手柄热插拔的 DSI 显示 bug；
- 规避：**先唤醒/连好手柄，再启动游戏**；游戏中不要让手柄休眠/断开；
- `j716f-bt-reconnect` 在检测到 Wine 游戏运行时会跳过自动重连，避免热插拔。

### Q7. Steam 登录/更新很慢

- Steam CDN 国内慢；可用 akamai 镜像 + hosts/gai.conf（历史方案见 [docs/ref-18](ref-18.md)）；
- 更新包可用 aria2 多线程预取；发布版建议用户自行安装 Steam（见 [docs/85](85-发布版构建手册.md)）。

### Q8. 退出 Steam 后黑屏 / 回不到桌面

本项目是纯 Game Mode（无桌面）。退出 Steam 后可能停在 gamescope/greetd 的空白会话，属预期；
需要桌面请自行安装 labwc/XFCE，或 SSH 进系统操作。

---

## 声音 / 麦克风

### Q9. 扬声器无声

- 检查 UCM：`/usr/share/alsa/ucm2/conf.d/sm8250/Lenovo Xiaoxin Pad Pro 2021.conf` 与
  `Qualcomm/sm8250/J716F-HiFi.conf` 是否部署（见 `configs/ucm/`）；
- 检查 `speaker-mixer.service` 是否运行、CS35L41 是否 probe；
- `aplay -l`、`alsamixer`、`dmesg | grep -i cs35l41`。

### Q10. 内置麦克风为什么用不了？

已定位为硬件/DSP 限制：ES7210 阵列被 DSP MI2S 占用，mainline 下无法作为普通 ALSA 麦克风使用。
用 USB 麦克风/耳机麦。详见 [docs/78](78-麦克风方案.md)、[docs/45/46](45-麦克风定位报告.md)。

---

## 网络 / 蓝牙

### Q11. WiFi 被关了打不开

`nmcli radio wifi off` 可能持久化；initramfs 救援脚本会重写
`/var/lib/NetworkManager/NetworkManager.state`（`WirelessEnabled=true`），也可手动：

```bash
sudo nmcli radio wifi on
sudo systemctl restart NetworkManager
```

### Q12. 蓝牙一开就挂 / 扫描卡死

- 本项目 baseline 使用 **BlueZ 5.72**，5.87 存在挂死问题（服务已禁用）；
- 先检查 `systemctl status bluetooth`、`rfkill list`、`btmgmt info`；
- 手柄先连后开游戏（见 Q6）；详见 [docs/73](73-蓝牙问题全集.md)、`docs/device/`。

### Q13. 5GHz WiFi 不稳 / 断流

- 记录过 WCN6855 电源管理/漫游问题；可尝试固定信道/关闭节能；
- `iw dev wlp1s0 set power_save off`，NetworkManager 配置 `wifi.powersave=2`。

---

## 刷机 / 发布

### Q14. 怎么进入 mini-recovery？

```bash
sudo python3 /home/j716f/tools/write_bcb.py --command boot-recovery   # 或 j716f-reboot-recovery
```

- 写 `misc` 前 32 字节为 `boot-recovery`，ABL 下次启动引导 `recovery_a/b`；
- recovery 会自己清 BCB，不会循环；
- 按键组合仍未确认（音量加+电源待测）。

### Q15. 卡刷后起不来

- 先确认 payload 的 SHA256SUMS 校验通过；
- boot.img 必须是有效 Android boot 镜像（`ANDROID!`）；
- rootfs.img.gz 写入是否完整（大小/分区空间）；
- 发布版 boot.img 的 cmdline 不能带 `systemd.mask=j716f-gamemode*`；
- 重进 recovery 再刷一次；仍不行用 PC 线刷救。

### Q16. 线刷 `fastboot flash userdata` 失败

- 确认 ABL 解锁（`fastboot getvar unlocked` = yes）、分区大小 ≥ 镜像；
- 部分 ABL 对 `userdata` 刷写有限制；备选：mini-recovery + SD 卡 payload（[docs/84](84-线刷卡刷双路线.md)）；
- 注意 sparse 镜像（`rootfs.simg`）不要再用 `-S` 拆包。

### Q17. 刷完还能回 Android 吗？

`userdata` 被覆盖；回 Android 需要**原厂完整固件**（LMSA/QFIL 线刷）。本项目不附带原厂固件。
刷机前请自行备份并确认能拿到原厂包。

### Q18. 首启没有网络/没有键盘怎么配网？

- 有键盘保护套：`Ctrl+Alt+F2` 进 tty2，`nmtui`；
- 无键盘：SD 卡根目录建 `/j716f/wifi.txt`（`SSID=...`、`PSK=...`），重启后首启脚本自动配网；
- 发布版首启先要求自定义用户名/密码（tty1 自动登录），设完重启再配网。

### Q19. 怎么换用户名？

发布版首启会提示自定义；已装好的系统手动改见 [docs/86](86-用户名通用化与首启自定义.md)：
`groupmod` + `usermod -l -d` + 批量替换引用 + `daemon-reload`，务必先停 greetd。

### Q20. 刷完触摸/键盘/音量键不对？

- 触摸：Goodix 驱动 + DTS 中断/复位配置；
- 键盘保护套：i2c-hid（i2c-4 @0x61）；
- 音量键：`j716f-volume-keys` + udev/uinput 规则；
- 检查是否用了错误的 DTB（btfix/实验版）。

---

## 性能 / 温度

### Q21. 为什么玩 3D 游戏很卡？

瓶颈是 Snapdragon 870 对 x86 的 FEX 转译 + 内存带宽/GPU 调频，不是发行版问题；
建议 ARM 原生、2D/独立游戏或串流。详见 [docs/90](90-性能调优与兼容性.md)。

### Q22. 能不能长期 3.2GHz？

不建议。3.2 只在短 burst 有意义；长时间会触发供电/温度问题。建议 3.2 burst + 2.84 持续 + GPU 525。
详见 [docs/76/77](76-CPU3.2长期热控方案.md)。

### Q23. 充电时玩游戏发热/掉电

- 充电策略按温度分档限流（1.6A → 1.0/0.8/0.5/0.3A），满电 100% 后涓流；
- 见 [docs/76](76-CPU3.2长期热控方案.md)、`j716f-charge-guard` 服务。

---

## 其它

### Q24. 能休眠吗？

baseline 禁用了 suspend（避免 USB gadget/网络挂掉）；需要自行测试。

### Q25. 系统里一堆 `j716f-*` 服务失败

先 `systemctl --failed` 看具体单元；常见原因是缺少硬件/配置路径或服务被刻意 mask（如蓝牙 5.87）。
发布版会清理调试用单元；自建系统请按 `configs/README.md` 部署。

### Q26. 如何备份当前系统？

- 全量 rootfs：`tar` 打包（或 `dd` 到镜像）；
- boot：`dd if=/dev/disk/by-partlabel/boot_a of=boot_a.raw bs=1M`；
- GPT：`sgdisk --backup=... /dev/sdX`；
- 详见 [docs/82](82-刷机包发布方案.md) 与 [docs/85](85-发布版构建手册.md)。
