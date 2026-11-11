# release/ — 发布版构建与安装工具

把一台已经跑起来的 J716F 的系统，打包成可给别人线刷/卡刷的**发布版镜像**。

## 1. 文件说明

### 设备侧（在平板上运行）

| 文件 | 作用 |
|---|---|
| `build-rootfs.sh` | 主构建脚本：rsync 当前系统 → 20G ext4 镜像 → 清理 → 首启 → 导出 boot.img → gz/simg |
| `postfix-image.sh` | 构建后修补（删除 netconsole 等调试残留并重新生成 gz/simg/哈希） |
| `fix-image-users.sh` | 修补已构建镜像里的用户名/uid 残留（yp 组、`/run/user/1001`） |
| `rootfs/sanitize.sh` | 清理个人数据、重置 machine-id/SSH key、默认密码、fstab label |
| `rootfs/rename-user-in-image.sh` | 构建期把 `yp`→占位 `player`，家目录统一 `/home/j716f`，删除未用 `alarm` 账户并规范 uid=1000 |
| `rootfs/j716f-firstboot.sh` | 首启：resize2fs、machine-id、SSH key、`/.boot-ok`、setup console 切换、SD wifi.txt |
| `rootfs/j716f-firstboot.service` / `j716f-boot-ok.service` | 首启 / 启动成功标记 |
| `rootfs/j716f-user-setup.sh` + `j716f-user-setup-profile.sh` | 首启让用户自定义登录名/密码（家目录固定） |
| `rootfs/trim_boot.py` / `fix_boot_cmdline.py` | 裁剪 boot.img 到有效长度；去掉遗留的 `systemd.mask=j716f-gamemode*` |
| `rootfs/j716f-reboot-recovery.sh` | 系统内一键重启进 mini-recovery（卡刷入口） |
| `steam/j716f-install-steam.sh` + `j716f-steam-install.service` | 发布版（无 Steam）在用户设备上安装 Steam 的封装（不含 Valve 客户端） |

### PC 侧（Windows）

| 文件 | 作用 |
|---|---|
| `lineflash/install.ps1` | 线刷安装器：校验哈希/机型/分区 → `fastboot flash boot_a/b + userdata` → `set_active a` → reboot |
| `make-package.ps1` | 组装发布包目录 `j716f-arch-steam-<ver>/`（含 fastboot、README、NOTICE、SHA256SUMS） |
| `README-TEMPLATE.md` | 给最终用户的安装说明模板（含首启自定义用户名、配网、装 Steam） |
| `NOTICE-SOURCE.md` | 许可证/固件来源/Valve 合规声明 |

## 2. 构建发布镜像（设备上）

前提：SD 卡挂在 `/mnt/sdcard`（建议 ≥40 GB 空闲），已装 `android-tools`（`img2simg`）、`gdisk`、`rsync`、`zstd`。

```bash
# 0) 资产已放到 /home/jyp/release-assets（或脚本所在目录）
sudo /home/yp/release-assets/build-rootfs.sh --inspect        # 只看清理目标与空间
sudo /home/yp/release-assets/build-rootfs.sh --strip-steam    # 发布版（不含 Steam/FEX）
```

产物（默认 `/mnt/sdcard/j716f-release/`）：

| 文件 | 说明 | 参考大小 |
|---|---|---|
| `boot.img` | 当前 boot_a 裁剪 + 去 mask | ~30 MB |
| `rootfs.img` | 20G ext4 逻辑镜像 | 20 GB（稀疏） |
| `rootfs.img.gz` | 卡刷 payload | ~2.2 GB |
| `rootfs.simg` | 线刷（Android sparse） | ~5 GB |
| `SHA256SUMS` / `manifest.txt` | 校验和/清单 | — |

> `--strip-steam` 会排除 `/opt/steam-arm`、`/opt/fex-rootfs`，并安装"用户自己装 Steam"的 setup 资产。
> 不做 strip 时镜像会包含 Steam/FEX 二进制（仅适合自用；公开分发注意 Valve 条款）。

### 构建后检查

```bash
IMG=/mnt/sdcard/j716f-release/rootfs.img
mkdir -p /mnt/chk && sudo mount -o loop,ro "$IMG" /mnt/chk
ls /mnt/chk/home/j716f                    # 空骨架
grep -E '^(player|yp|alarm):' /mnt/chk/etc/passwd
ls /mnt/chk/etc/j716f                     # user-setup-pending / steam-setup-mode
ls /mnt/chk/etc/systemd/system/multi-user.target.wants | grep j716f
sudo umount /mnt/chk
```

如果构建后才发现遗漏（例如 netconsole），用 `postfix-image.sh` / `fix-image-users.sh` 修补并重新生成哈希。

## 3. 卡刷包（SD 卡）

```bash
sudo python3 /home/yp/tools/make-cardflash.py \
  --boot /mnt/sdcard/j716f-release/boot.img \
  --rootfs /mnt/sdcard/j716f-release/rootfs.img.gz \
  --out /mnt/sdcard --version v1.0.0 \
  --zip /mnt/sdcard/j716f-update.zip \
  --update-binary /home/yp/tools/cardflash/update-binary \
  --qbootctl /root/qbootctl-src/build-static/qbootctl
```

- `/mnt/sdcard/j716f/` 是 mini-recovery 读取的裸文件布局；
- `j716f-update.zip` 是给 TWRP/其他 recovery 的兼容包；
- 设备进 recovery：`sudo j716f-reboot-recovery`（或 `write_bcb.py`）。

## 4. 线刷发布包（PC）

把设备产出的 `boot.img`、`rootfs.simg` 拷到 PC 的输入目录，然后：

```powershell
powershell -ExecutionPolicy Bypass -File release\make-package.ps1 `
  -InputDir D:\j716f-build -Version v1.0.0 -OutDir D:\j716f-release `
  -PlatformTools D:\1s\platform-tools
```

得到 `j716f-arch-steam-v1.0.0/`（含 `images/`、`install.ps1`、`tools/platform-tools`、README/NOTICE/SHA256SUMS）。

## 5. 用户首启流程（发布版）

```
线刷/卡刷完成
  → 首次启动进入 setup console（tty1 自动登录占位用户）
  → 输入自定义用户名 + 密码（家目录固定 /home/j716f）
  → 重启
  → 配网（键盘 nmtui，或 SD 卡 /j716f/wifi.txt）
  → sudo j716f-install-steam.sh（用户设备自行下载 Valve 客户端）
  → 重启进入 Steam Big Picture
```

## 6. 测试状态（如实记录）

| 项目 | 状态 |
|---|---|
| rootfs 构建（rsync/清理/首启/用户名规范） | ✅ 已在本机跑通 |
| boot.img 裁剪 + 去 mask | ✅ |
| mini-recovery 写入 recovery_a/b | ✅ |
| 卡刷（仅 boot.img payload） | ✅ 实测通过 |
| 卡刷（rootfs 整盘） | ⏳ 流程已实现，尚未在实机做"清数据重装"验证 |
| 线刷 `fastboot flash userdata` | ⏳ 未实测（需要 PC + USB） |
| Steam 首启安装 | ⏳ 封装已完成，未在全新镜像上实测 |

## 7. 注意

- 刷 `userdata` 会清空原 Android/数据；回 Android 需要原厂固件；
- 不要覆盖 `persist`/`modem`/`modemst*`/`fsg` 等设备唯一分区；
- 发布包内不要包含 Valve 客户端/账号数据/个人数据；
- GPL：内核源码对应关系见 `kernel/README.md` 与 `NOTICE-SOURCE.md`。
