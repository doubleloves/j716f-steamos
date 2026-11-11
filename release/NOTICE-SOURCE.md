# NOTICE / 源码与许可证说明（发布版模板）

本发布包是 **Lenovo Xiaoxin Pad Pro 2021（TB-J716F）** 的第三方系统镜像，非联想官方。
使用/分发前请阅读并遵守以下说明。

## 1. 包含的第三方组件

| 组件 | 位置 | 许可证 / 说明 |
|---|---|---|
| Linux 内核（6.13.0-sm8250 定制树） | `boot.img`、`/lib/modules` | GPL-2.0；对应源码与补丁见下 |
| Arch Linux ARM 用户态 | rootfs | 各包按自身许可证（保留 `/usr/share/licenses`） |
| Mesa / Turnip / gamescope / MangoHud / BlueZ / FEX | rootfs | 按各自开源许可证 |
| Steam 客户端（如包含） | `/opt/steam-arm` | Valve Steam Subscriber Agreement，**不可再分发客户端文件**（建议 `--strip-steam` 发布） |
| 高通固件（ADSP/CDSP/WCN6855/Adreno 等） | `/lib/firmware/qcom` | 专有文件，来源为本机原厂固件；再分发存在法律灰区 |

## 2. GPL 源码

- 内核源码 / 配置 / 补丁：`<填 GitHub 仓库或网盘地址>`（或随包提供 `kernel-src.tar.gz`）
- 内核版本：`<填 uname -r>`；配置：`<填 .config 文件名>`
- 修改过的驱动/设备树补丁：见仓库 `patches/`

## 3. 个人信息

- 本镜像**不包含**任何人的账号、密码、SSH 密钥、WiFi 密码、蓝牙配对、Steam 登录态或游戏库。
- 默认用户 `player` / 密码 `j716f`，请使用者首次登录后修改。

## 4. 风险

刷机会清空设备数据、可能导致设备无法启动、失去保修。作者不对任何损失负责。
仅支持 TB-J716F；禁止用于其他型号。

## 5. 不提供

- 不提供付费刷机服务；
- 不包含原厂联想固件；
- 不承担数据恢复义务。
