# Kernel source organization

本目录按用途分成两部分：

| 目录 | 内容 |
|---|---|
| `linux-6.13-j716f/` | **实机运行的内核**（6.13.0-sm8250）的设备补丁集、`.config`、DTB 源文件、构建脚本 |
| `linux-6.15.11-j716f-ubuntu/` | 早期 `j716f-ubuntu` 项目的 6.15.11 补丁/配置快照（含 DSI、麦克风、指纹相关补丁） |

## 使用 6.13 补丁（推荐）

```bash
cd linux-6.13-j716f
./build.sh              # 自动下载 linux-6.13、打补丁、编译 Image/dtbs/modules
```

补丁相对 **上游 v6.13** 生成；`patches/0001-j716f-6.13.patch` 可用 `patch -p1` / `git apply` 应用。

## 为什么不在仓库里放完整内核树

Linux 内核为 GPL-2.0，仓库里保存完整内核树会让仓库体积膨胀到 GB 级，且与上游重复。
本仓库采用 **patch + config + 构建脚本** 的方式，任何人都能从上游复现同一内核。

如果希望以 **git submodule** 方式管理完整树（例如需要频繁改内核），可以在你准备好独立
内核仓库后执行：

```bash
# 1) 建独立内核仓库（在任意目录）
mkdir linux-j716f && cd linux-j716f
git init -b main
# 解压 linux-6.13、应用 patches/0001-j716f-6.13.patch、放入 config
git add -A && git commit -m "linux 6.13 + J716F device patches"
git remote add origin https://github.com/<you>/linux-j716f.git
git push -u origin main

# 2) 在本仓库添加 submodule
cd j716f-steamos
git submodule add https://github.com/<you>/linux-j716f.git kernel/linux-6.13-j716f/tree
git commit -m "kernel: add linux 6.13 source submodule"
```

## 6.15.11 补丁集是什么

`j716f-ubuntu` 是设备早期 Ubuntu 阶段的适配补丁集合（DSI 面板、麦克风 ES7210、指纹等），
对 6.15 系列的移植有参考价值；它不是当前运行内核，仅作为资料保留。

## 许可

内核补丁/配置遵循 Linux 内核的 GPL-2.0；使用/再分发时请遵守 GPL。内核源码获取方式见上文。

> 注意：6.13 补丁里包含设备原厂/社区来源的驱动代码（`dbmdx`、`es7210`、`camss`、充电等），
> 其原始出处与许可需自行核对；对外分发前请确认合规。
