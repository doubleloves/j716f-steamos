# 28 · DiRT Rally 闪退修复（关闭 Steam 覆盖层）（2026-10-27）

## 结论：**已修复 ✅**

DiRT Rally（AppID 310560）闪退原因 = **Steam 游戏内覆盖层（Overlay）注入 32 位游戏时，在 ARM64 Proton + FEX 的 32 位模拟路径下导致栈溢出崩溃**。

关闭覆盖层后游戏正常进入主菜单并可游玩（Steam 显示已游玩 4 分钟）。

## 排查过程（证据链）

| 步骤 | 结果 |
|---|---|
| 游戏位数检查 | `drt.exe` = **32-bit x86**（PE32） |
| 首次启动 | 正常安装 DirectX redist（x86+x64）、创建 wine 前缀 |
| 启动后 ~10-17 秒 | 游戏窗口 "DiRT Rally" 2560x1600 出现、能看到主菜单 |
| 随后 | 闪退 |
| Proton 日志 | `err:steamclient:steamclient_init unable to load native steamclient library`（首次手动运行） |
| 崩溃异常 | `EXCEPTION_ACCESS_VIOLATION` 反复触发 → `EXCEPTION_STACK_OVERFLOW`（1833 次 AV + 3 次栈溢出） |
| DXVK 状态 | DXVK v2.7.1 正常加载、winevulkan/vulkan 正常（画面能渲染说明 D3D11 没问题） |

**根因**：32 位游戏 + Steam 覆盖层（gameoverlayrenderer 注入）在 FEX 32 位模式下破坏调用栈 → 栈溢出崩溃。不是 D3D/GPU 问题。

## 修复方法

```bash
# 关闭该游戏的 Steam 覆盖层（持久化到 /etc/steam-arm/titles.conf）
steam-arm-config profile 310560 overlay=off
```

已生效：`/etc/steam-arm/titles.conf` 中 `310560 overlay=off`

## 同时处理
- 对 **Human Fall Flat (477160)** 也设置了 `overlay=off`（它也是 32 位游戏，之前崩在 dxgi.dll；关掉覆盖层后值得再试一次）
- 清理了调试用的 180MB Proton 日志、截图、dmesg 残留
- 去掉了临时 PROTON_LOG 环境变量，Steam 不带调试参数重启

## 经验：ARM64 Proton 跑 32 位游戏汇总

| 游戏 | 位数 | 状态 | 备注 |
|---|---|---|---|
| DiRT Rally | 32-bit | ✅ 可玩 | 需 `overlay=off` |
| ICEY | 64-bit | ✅ 可玩 | |
| Balatro | 64-bit | ✅ 可玩 | |
| Escape from Duckov | 64-bit | 待验证 | |
| GTA VC 决定版 | 64-bit | 待验证 | 需 Rockstar 启动器 |
| Human Fall Flat | 32-bit | 待验证（已关覆盖层） | 曾崩在 dxgi.dll |

## 如果 32 位游戏仍有问题，可继续尝试（按顺序）
1. `steam-arm-config profile <appid> overlay=off`（本次有效）
2. 换 Proton Experimental：`steam-arm-config compat <appid> ge` 或手动映射
3. `steam-arm-config profile <appid> tso=off`
4. `steam-arm-config profile <appid> env='PROTON_USE_WINED3D=1'`（放弃 DXVK，走 OpenGL，慢但兼容）
5. 仍不行 → Steam Link 串流（PC 渲染）
