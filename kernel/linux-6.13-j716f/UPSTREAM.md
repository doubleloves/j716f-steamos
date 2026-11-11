# Upstream base

本目录的设备改动基于 **Linux v6.13 上游**，而不是某个厂商内核。

| 项目 | 值 |
|---|---|
| 上游源码 | <https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.13.tar.xz> |
| 文件大小 | `148523052` bytes |
| SHA-256 | `e79dcc6eb86695c6babfb07c2861912b635d5075c6cd1cd0567d1ea155f80d6e` |
| 补丁 | `patches/0001-j716f-6.13.patch`（`patch -p1` 应用，已 dry-run 验证） |
| 配置 | `config-6.13-j716f`（实机 `.config`） |
| 设备树 | `dts/sm8250-lenovo-j716f.dts`、`dts/sm8250-lenovo-j716f-btfix.dts` |

## 校验

```bash
curl -LO https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.13.tar.xz
echo "e79dcc6eb86695c6babfb07c2861912b635d5075c6cd1cd0567d1ea155f80d6e  linux-6.13.tar.xz" | sha256sum -c -
tar xf linux-6.13.tar.xz
cd linux-6.13
patch -p1 < ../patches/0001-j716f-6.13.patch
cp ../config-6.13-j716f .config
```

## 关于 `src/`

`src/` 里直接放了 **80 个被修改/新增的源文件**（约 2.5 MB），按内核树相对路径组织，方便在 GitHub 上直接浏览
设备相关的 DTS、驱动、Makefile/Kconfig 改动；它不是完整内核树。

- 需要**完整内核树**：运行 `import-full-tree.sh`，会在 `tree/` 下生成完整源码树（默认被 `.gitignore` 忽略）。
- 需要**提交完整树**：先确认体积可接受（解压后约 1.6 GB，git 对象数百 MB，接近 GitHub 仓库体积建议上限），
  然后从 `.gitignore` 里去掉 `kernel/*/tree/` 并 `git add -f`。
