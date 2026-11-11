# 18 · SteamOS 风格改造成功记录（2026-10-26 晚）

> **成果：联想小新 Pad Pro 2021（ARM64/骁龙870）成功运行 Valve 原生 ARM64 Steam 客户端 + Big Picture 界面。**
> 开机直接进入 Steam Big Picture（SteamOS 默认行为），退出 Steam 后进入 XFCE 桌面。

## 一、达成状态

| 项目 | 状态 |
|---|---|
| **Steam 客户端** | ✅ Valve 官方原生 **ARM64 客户端**（Steam Frame 版，`-deckard -steamos3 -gamepadui`） |
| **Big Picture** | ✅ 全屏 2560×1600 登录页正常显示 |
| **触屏输入** | ✅ Steam 自带屏幕键盘可用（触屏友好） |
| **开机流程** | ✅ 上电 → 自动登录 j716f → startx → **Steam Big Picture**；退出 Steam → XFCE 桌面 |
| **x86 游戏支持** | ✅ FEX-Emu（x86/x86_64 Linux 游戏转译）+ Valve ARM64 Proton（Windows 游戏） |
| **GPU** | ✅ Turnip Adreno 650 Vulkan（DXVK 预期可用）+ glx-lax |
| **网络** | ✅ WiFi（NetworkManager + 托盘图标）；Steam CDN 已加速（hosts → akamai，29MB/s） |
| 存储 | ✅ rootfs 已扩到 107GB（从 12GB 镜像 resize） |

## 二、关键技术点（可复现）

### 1. 方案：Scrumpper/Steam-ARM（社区项目）
- 仓库：https://github.com/Scrumpper/Steam-ARM
- 原理：安装 Valve 为 **Steam Frame** 打包的原生 ARM64 Steam 客户端（Debian/Ubuntu ARM64），
  x86 Linux 游戏经 **FEX** 转译，Windows 游戏经 **Valve 的 ARM64 Proton**
- 要求：ARMv8.1+（LSE atomics）✓（Cortex-A77/A55）、Debian/Ubuntu、**SDL3**、Vulkan（Windows 游戏）

### 2. Ubuntu 24.04 缺 SDL3 的解决
项目要求 Ubuntu 25.10+，24.04 缺 `libsdl3-0/-image0/-ttf0`。解决：
```bash
# 临时加 questing 源下载 SDL3 + 新依赖，用完删除源
echo 'deb [trusted=yes] http://ports.ubuntu.com/ubuntu-ports/ questing universe main' > /etc/apt/sources.list.d/questing-temp.list
apt-get update -o Dir::Etc::sourcelist=... 
apt-get download libsdl3-0 libsdl3-image0 libsdl3-ttf0 libpng16-16t64 libwebp7 libwebpdemux2 libsharpyuv0
apt-get install -y ./*.deb   # 依赖从 noble 解析
rm -f /etc/apt/sources.list.d/questing-temp.list
```
验证：`apt-cache policy libsdl3-0`（Installed ✓）、`ldconfig -p | grep libSDL3`

### 3. Steam CDN 慢的解决（关键！）
- 问题：`client-update.steamstatic.com` 在国内 ~38KB/s（客户端更新 648MB 要 1 小时+）
- 解决：
  1. **aria2 多线程**直接从 akamai 镜像下载（`client-update.akamai.steamstatic.com`，**29MB/s**）
  2. 修改安装器：`CDN=`/`MANIFEST=` 换 akamai；`client_pkg()` 优先用本地 `/root/client.zip`
  3. 其余更新包（35 个、`zipvz`）用 manifest 解析后 aria2 批量下载，放入
     `~/.local/share/steam-arm/.local/share/Steam/package/`
  4. **/etc/hosts 重定向**（根治）：
     ```
     23.193.184.135 client-update.steamstatic.com # akamai-test
     ```
     + `/etc/gai.conf`: `precedence ::ffff:0:0/96 100`（IPv4 优先）
     之后 Steam 自己的下载也走 akamai，速度 7MB/s+

### 4. 0x3009 "Unexpected Transport Error" 的解决（关键！）
- 现象：Steam 启动弹 `Unexpected Transport Error (0x3009)`，CEF 日志：
  `Failed to connect to the bus: Failed to connect to socket /run/user/1000/bus`
- 根因：Steam 被 `su` 从 root 会话启动，**没有用户级 logind/dbus 会话**
- 解决：**把图形会话迁移到游戏用户 j716f**
  - `getty@tty1` 自动登录改 j716f
  - j716f 的 `.xinitrc`：先确保 `XDG_RUNTIME_DIR` + **dbus-daemon 会话总线**，再启动 Steam
  - SteamOS 风格：默认 `steam-arm --bigpicture`；退出后 `xfce4-session`
- 迁移后：`/run/user/1000/bus` 存在，CEF 无总线错误，Big Picture 正常

### 5. 触摸与输入
- 触摸：Goodix 触摸屏 → libinput → X 指针；Steam Big Picture 大按钮触屏友好
- **Steam 自带屏幕键盘**（Deck 风格，带 L2/R2）自动用于输入
- 备选：`/home/j716f/osk.py`（自研触摸键盘，XFCE 桌面会话自启动）
- 中文输入法：fcitx5 + 拼音（XFCE 桌面内；Steam 内输入英文即可）

## 三、日常使用

```bash
# 启动器（以 j716f 身份）
steam-arm              # 启动 Steam
steam-arm --bigpicture # Big Picture（默认开机即此）
steam-arm --desktop    # 桌面模式
steam-arm --shutdown   # 关闭 Steam

# 切换开机模式
sudo steamos-toggle    # 在「开机进 Steam」与「开机进桌面」间切换

# 配置菜单（组件/图形/游戏/手柄/卸载）
sudo steam-arm-config
```

- **退出 Steam** → 自动进入 XFCE 桌面（有键盘/输入法/浏览器/播放器）
- **回到 Steam**：桌面菜单里 "Steam ARM" 或命令行 `steam-arm --bigpicture`

## 四、游戏兼容性预期

| 类型 | 支持方式 | 预期 |
|---|---|---|
| ARM64 Linux 原生游戏 | 直接运行 | ✅ 最好 |
| x86 Linux 游戏 | FEX 转译 | ⚠️ 轻量/2D 可玩，3D 大作较卡 |
| Windows 游戏 | Valve ARM64 Proton + DXVK | ⚠️ 受 GPU/CPU 转译限制，部分可玩（项目说明：Adreno 6xx~8xx DXVK 预期可用） |
| 云游戏/串流 | Moonlight / Steam Remote Play | ✅ 推荐重度 3A |

## 五、已知限制

1. **性能**：骁龙870 CPU 转译 x86 有开销；重度 3D 游戏不现实，建议玩 ARM 原生/2D/独立游戏或串流。
2. **游戏下载**：Steam 游戏内容走 `steamcontent.com` CDN，未做加速（如需可仿照 hosts 方案）。
3. **首次登录**：需要 Steam 账号密码（触屏键盘输入）；Steam Guard 可能需要邮箱/手机验证。
4. **音频**：声卡驱动已识别（ADSP+4×CS35L41），但主线路由未完全打通（Steam 声音可能无声，属于 mainline WIP）。
5. **槽位重试计数**：每约 7 次启动需 `fastboot set_active a`。
6. **休眠**已禁用（避免 USB gadget 挂掉）。

## 六、产物清单

| 文件 | 说明 |
|---|---|
| `tablet/osk.py` | 自研触摸键盘（桌面用） |
| `tablet/migrate-user-session.sh` | 会话迁移到 j716f + SteamOS 启动流程 |
| `tablet/fix-dbus-session.sh` | dbus 会话修复 |
| `tablet/hosts-accel2.sh` / `steam-accel-run.sh` | Steam CDN 加速 |
| `tablet/prefetch-pkgs.sh` / `dl-client.sh` | aria2 预取 Steam 包 |
| `work/steam-arm-install.sh` | Steam ARM 安装器原版（998KB） |
| `work/steam-arm-install-patched.sh` | 加速补丁版（本地客户端包 + akamai CDN） |
| `work/steam-arm-README.md` / `-COMPAT.md` / `-HOW.md` | 项目文档 |
| `inspect/screen-steam6.png` | ✅ Big Picture 登录界面截图 |
| 平板内：`/root/client.zip`、`/root/steam-pkgs/` | Steam 客户端与更新包 |

## 七、重要命令备忘（在平板上）

```bash
# 查看 Steam 状态
ps aux | grep steam
tail -f /home/j716f/.local/share/steam-arm/.local/share/Steam/logs/cef_log.txt

# 重新加速更新（如果 Steam 更新又变慢）
sudo bash /root/prefetch-pkgs.sh 2>/dev/null || true

# 截图（以 j716f 身份）
su - j716f -c 'DISPLAY=:0 import -window root /tmp/s.png'
```
