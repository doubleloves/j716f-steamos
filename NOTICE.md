# NOTICE — 许可证、来源与合规说明

本仓库是第三方社区项目，**不隶属于 Lenovo（联想）或 Valve**。

## 1. 仓库内容许可

- `scripts/`、`tools/`、`recovery/`、`release/`（代码）：MIT，见 [LICENSE](LICENSE)
- `docs/`（文档）：CC BY-SA 4.0，转载/修改请保留出处
- `kernel/` 中的内核补丁/配置快照：源自 Linux 内核（GPL-2.0）与社区补丁，按原许可证使用

## 2. 不包含的内容

- **Valve Steam 客户端文件**：Steam 客户端由用户在自己的设备上从 Valve 官方渠道下载安装
  （发布版包含 `j716f-install-steam.sh`，只负责调用安装器/下载，不含 Valve 二进制）；
- **Steam 账号/登录态/游戏库**；
- **联想原厂固件**（`super`、原厂 boot、vendor 等）；
- **高通专有固件**（`/lib/firmware/qcom/*`）：不随本仓库分发；发布镜像中如包含，
  来源为用户设备原厂固件，再分发存在法律灰区，使用/分发者自行承担风险；
- 他人的个人数据、WiFi 密码、SSH 密钥、序列号等。

## 3. 第三方组件

| 组件 | 许可证/来源 |
|---|---|
| Linux 内核及补丁 | GPL-2.0，源码见内核仓库/发布页 |
| Arch Linux ARM 用户态 | 各包许可证，保留 `/usr/share/licenses` |
| gamescope / MangoHud / Mesa / FEX / BlueZ | 各自开源许可证 |
| Steam-ARM 项目 | 见 https://github.com/Scrumpper/Steam-ARM |
| qbootctl | 见 https://github.com/linux-msm/qbootctl |
| 高通固件 | 专有，不提供 |
| 设备原厂/vendor 驱动代码（kernel 补丁中的 dbmdx/es7210/camss 等） | 来源与许可需自行核对，见 `kernel/README.md` |

## 4. 风险声明

刷机可能清空数据、导致设备无法启动、失去保修。仅支持 **Lenovo TB-J716F**。
作者不对任何直接或间接损失负责；请自行备份并确认理解风险。
