# 贡献指南

欢迎提交 issue / PR。这个项目来自一台上手改造的平板，很多结论都带设备/环境限定，请保持可复现性。

## 提交前

- 不要提交个人数据：用户名、家目录、WiFi 密码、SSH 密钥、序列号、IP/MAC、Steam 账号/登录态、
  以及含这些信息的截图（截图里的二维码/头像请先打码）；
- 不要提交二进制镜像（`*.img`、`*.gz`、`*.simg`）和完整内核树；内核用 **patch + config + `src/` 改动文件**；
- 大文件走 Releases/网盘，不进 git。

## 代码风格

- Shell：`#!/bin/bash` + `set -euo pipefail`（busybox 兼容的脚本用 `#!/bin/sh` 并避免 bash 扩展）；
- Python：Python 3.8+，标准库优先，文件头写用途与用法；
- 设备路径：发布版统一 `/home/j716f`；如果涉及用户名，用 `getent passwd 1000` 动态获取，不要写死；
- 服务单元：`WantedBy=multi-user.target`，日志进 journal，条件用 `ConditionPathExists*`。

## 测试要求

涉及设备的改动请至少说明：

1. 设备/内核/发行版版本（`uname -a`、`pacman -Q` 关键包）；
2. 改动前后对比（日志/命令输出/截图，注意脱敏）；
3. 回滚方式（尤其涉及 GPT、boot、recovery、userdata 的操作）。

## 文档

- 新问题的排查记录请放 `docs/`，命名沿用 `NN-标题.md`；
- 涉及刷机/分区/ABL 的内容请交叉引用 `docs/83`、`docs/84`、`docs/85`，避免读者误操作；
- 截图请放 `docs/images/` 并更新 `docs/screenshots.md`。

## 许可

- 代码：MIT；文档：CC BY-SA 4.0；
- 内核补丁遵循 GPL-2.0；第三方组件保留其许可证；
- 提交 PR 即表示你同意以相应许可证发布你的贡献，并确认有权提交相关内容。
