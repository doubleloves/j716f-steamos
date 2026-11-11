# 83 · 彻底解决 ABL 槽位重试计数（目标：永远不需要因为"重启 7 次"接电脑）

日期：2026-11-10
关联：`docs/82`（刷机包发布方案）、`docs/84`（线刷 + 卡刷双路线）

---

## 0. 结论（先看这段）

1. **之前"计数在 RPMB、Linux 改不了"的结论需要修正**。Qualcomm 的 A/B 槽位状态实际存放在 **GPT 分区表的分区属性位（bits 48–63）** 里，而不是 `misc`；Linux 有办法读写它。postmarketOS 的 **qbootctl** 就是做这件事的工具，在 SDM845/SM7225/SM8250 一类的 Qualcomm UEFI ABL 设备上已被广泛验证（OnePlus 6、SHIFT6mq、Fairphone 4 等）。
2. 机制（已在本机实测确认）：槽位重试计数存在 **GPT 分区属性字节**（boot_a 的分区项 offset 54）的 **bits 3–5**，初值 7，每次启动**如果没有被标记 "boot successful"**（同一字节 bit 6）就递减；到 0 时 bootloader 把该槽标记为 unbootable 并切到另一个槽（另一个槽通常也起不来 → 卡 fastboot）。**只要把 bit 6 置 1，计数就不再递减。**
3. 方案：装 `qbootctl`，加一个 systemd 服务，**每次启动执行 `qbootctl -m`**（只置 bit 6，不动其它位）。**已在本机验证通过**（2026-11-10，连续 4 次重启：success 位保持、计数不再递减、ABL 日志不再打印 retry count 行、不需要 PC）。
4. 再加一层"最终兜底"（同样不需要 PC）：把 mini-recovery 装进 `recovery_a/recovery_b`（100 MiB，空闲未用），用 `misc` 的 BCB command `boot-recovery` 从系统内重启进入；recovery 里可以重刷 boot/rootfs、修正槽位标志。详见 `docs/84`。
5. 本机 `slot-bcb.py` 写 `misc` 无效是对的——因为 A/B 状态根本不看 `misc`；但"只能靠 PC 看门狗"这个结论**已被推翻**（见 §4 的实测记录）。

> 重要前提：写 GPT 是有风险的（分区表写坏 = 变砖级别）。所以流程是"先只读侦察 → 备份 GPT → 单次手动验证 → 10 次重启验证 → 再开服务"。

---

## 1. 证据链

### 1.1 上游资料

- **postmarketOS wiki（Android A/B Slots）**：Qualcomm 的 A/B 实现在 **GPT 分区属性字段** 和 **UFS bBootLunEn**；"Upgrade roll-back is implemented using a counter, it is initialised at 7 and decremented every time you boot the system **unless you set the "boot successful" flag from userspace**. When the counter reaches 0 the bootloader will automatically mark the slot as "unbootable" and switch to the other slot."；并明确说装好 qbootctl 服务后"automatically set the boot successful flag on every boot"，此问题不再发生。
  <https://wiki.postmarketos.org/wiki/Android_AB_Slots>
- **Debian wiki（OnePlus 6）**："qbootctl … is needed to reset slot-retry-count for the active slot to 7 on every boot, otherwise that flag gets decremented at every boot and the device will consider it unbootable when it reaches 0…"，并给出 systemd 单元需要 `WantedBy=multi-user.target` 的配置。
  <https://wiki.debian.org/InstallingDebianOn/OnePlus/OnePlus6>
- **qbootctl 源码（Qualcomm bootctrl HAL 的 Linux 移植）**：状态在 GPT 分区属性的 `AB_FLAG_OFFSET`（分区项偏移 54）：
  - `AB_PARTITION_ATTR_SLOT_ACTIVE = 0x04`（bit 2）
  - `AB_PARTITION_ATTR_BOOT_SUCCESSFUL = 0x40`（bit 6）
  - `AB_PARTITION_ATTR_UNBOOTABLE = 0x80`（bit 7）
  - 激活时写入 `0xF`；`markBootSuccessful()` 只做 `|= 0x40`。
  源码：<https://salsa.debian.org/DebianOnMobile-team/qbootctl>（`bootctrl_impl.c`、`gpt-utils.h`、`qbootctl.c`）
- **旧版 Qualcomm HAL 布局**（`hardware/bsp/qcom/boot_control/gpt.h`）：bits 48–51 priority、52–55 tries remaining、56 successful；与新版布局不同，所以必须实测确认本机是哪一种（见 §4）。
- AOSP `misc` 的 BCB 只用于 recovery command/slot_suffix（`bootloader_message_ab`），**不是** Qualcomm 的 A/B retry 状态；这解释了 `slot-bcb.py` 为什么无效。

### 1.2 本机 GPT dump 的旁证

用 `tools/gpt_slot_attrs.py` 解析 `backup/partitions/sde-gpt-head.bin`（2026-11-06 的 dump）：

```
boot_a  attr=0x0027000000000000   属性字节54=0x27  → new: active=1
boot_b  attr=0x0002000000000000   属性字节54=0x02  → new: active=0
aop_a   attr=0x1004000000000000   属性字节54=0x04  → new: active=1
aop_b   attr=0x0000000000000000   属性字节54=0x00  → new: active=0
```

- 所有 `*_a` 分区的字节 54 都带 `0x04`（active 位），`*_b` 都没有 → **本机 GPT 确实使用 Qualcomm A/B 属性位**；
- `boot_a` 与 `boot_b` 的额外低位不同（0x27 vs 0x02），正是"priority/tries"这类每槽状态的候选位置；
- 全部数据里 `misc` 与 A/B 状态无关 → 之前的 RPMB 结论是错误归因。

> 注意：这个 dump 是旧的、也是 Ubuntu 时期的；真正判断必须在当前设备上重新 dump（§3 Step 1）。

---

## 2. 两种属性布局（工具会同时打印）

| 字段 | 新版（qbootctl / AOSP qcom bootctrl） | 旧版（bsp/qcom gpt.h） |
|---|---|---|
| 位置 | 分区项 offset 54（一个字节） | 64-bit attr 的 bits 48–63 |
| 激活 | bit 2 = `0x04` | 未单独定义（用 priority 区分） |
| 成功 | bit 6 = `0x40` | bit 56 |
| 不可启动 | bit 7 = `0x80` | 可能有独立位（未公开确认） |
| 重试 | 同字节低位（未公开完整定义） | bits 52–55（4 bit，初值 7） |

工具：`tools/gpt_slot_attrs.py`（只读；在 PC 上分析 dump，也可以在设备上直接读 `/dev/sda`…）。

```bash
python tools/gpt_slot_attrs.py backup/partitions/sde-gpt-head.bin
python tools/gpt_slot_attrs.py --dump-dir backup/partitions
```

---

## 3. 实测流程（设备恢复在线后照做，每一步都可回滚）

### Step 0 · 备份 GPT（必做，一次）

```bash
# 设备上（需要 gdisk 提供 sgdisk）
sudo pacman -S --needed gdisk
sudo mkdir -p /home/j716f/backup/gpt-$(date +%Y%m%d)
for d in sda sdb sdc sdd sde sdf; do
  [ -b /dev/$d ] || continue
  sudo sgdisk --backup=/home/j716f/backup/gpt-$(date +%Y%m%d)/$d.bin /dev/$d
  sudo sgdisk --verify /dev/$d
done
# 另外把每个 LUN 的 GPT 头 + 分区项也存一份（离线可读）
sudo /home/j716f/tablet/slot-attr-dump.sh /home/j716f/backup/slotattr-before
```

把 `slotattr-before` 和 `gpt-*/` 拷回 PC（或用 `tools/pull.py`）。

### Step 1 · 只读侦察（不改任何东西）

1. 设备在 Linux 下：
   ```bash
   sudo /home/j716f/tablet/slot-attr-dump.sh /home/j716f/slotattr-$(date +%m%d-%H%M)
   cat /proc/cmdline | tr ' ' '\n' | grep -i slot
   ```
2. **再从 PC 进 fastboot 看 ABL 自己的说法**（这一次需要 PC，之后就不用了）：
   ```
   fastboot getvar all > fastboot-all.txt
   ```
   重点记录：`current-slot`、`slot-retry-count:a/b`、`slot-successful:a/b`、`slot-unbootable:a/b`。
3. 把两边的数据对上：哪个字节/位对应 active、successful、unbootable、retry。可用：
   ```bash
   # PC 上
   python tools/gpt_slot_attrs.py --dump-dir slotattr-YYYYMMDD
   ```

### Step 2 · 构建 qbootctl（只读验证）

```bash
sudo /home/j716f/tablet/install-qbootctl.sh          # 构建 + 安装 + 只读 dump
# 输出应类似：
#   Current slot: _a
#   SLOT _a: Active 1 / Successful ? / Bootable 1
#   SLOT _b: Active 0 / ...
```

如果 qbootctl 的解读与 Step 1 的 fastboot 变量一致（A 槽 active、B 不 active），说明布局匹配，继续 Step 3；如果不一致，跳到 §4。

### Step 3 · 单次手动验证 + 10 次重启

```bash
sudo qbootctl -m            # 标记当前槽 successful（只写 GPT 属性）
sudo qbootctl               # 确认该槽 Successful: 1
sudo reboot
```

重启后：

```bash
sudo qbootctl               # Successful 应仍为 1（ABL 没有清掉）
```

再连续 `reboot` 8–10 次，确认：

- 不会进入 fastboot；
- 每次 `qbootctl` 显示当前槽 successful；
- （可选）每几次从 PC 看一次 `fastboot getvar slot-retry-count:a`，确认是 7/不变。

### Step 4 · 启用开机服务

```bash
sudo /home/j716f/tablet/install-qbootctl.sh --service
systemctl status j716f-mark-success.service
journalctl -u j716f-mark-success -b
```

以后每次启动自动 `qbootctl -m`；正常重启不再消耗 retry 计数。

### Step 5 · 回滚

- 服务：`sudo systemctl disable --now j716f-mark-success.service`；
- GPT：用 Step 0 的 `sgdisk --load-backup=/.../sde.bin /dev/sde` 恢复（**谨慎**，确认磁盘号）；
- 若已卡 fastboot：PC 执行 `fastboot set_active a && fastboot reboot`（老办法，但仍然有效）。

### 3.5 本机实测记录（2026-11-10 21:00，已完成 ✅）

> 结论先行：**已解决并验证**。连续 4 次重启不再递减计数、不再需要 PC；`j716f-mark-success.service` 已装好并每启动自动执行。

**① ABL 自己的日志就是"传感器"**

```bash
strings /dev/disk/by-partlabel/logfs | grep -i retry
# 修复前：Active Slot _a is bootable, retry count 6
# 重启一次后：Active Slot _a is bootable, retry count 5
# 标记成功后：这一行不再出现（ABL 认为该槽已 successful，不再走重试逻辑）
```

**② GPT 属性差异精确定位了计数位**

重启前后对比所有分区（含 GPT 属性、misc/devinfo/frp/secdata/storsec/uefivarstore/keystore/metadata/ssd/persist/logfs 等）：

```
唯一变化：boot_a 的属性 0x0037000000000000 -> 0x002f000000000000
byte54: 0x37 -> 0x2f
bits3-5: 0b110 (6) -> 0b101 (5)     ← retry count，与 ABL 日志 6→5 完全一致
其它分区内容 / GPT 属性：全部未变（logfs 因写日志而变）
```

由此确认本机布局：

| byte54 位 | 含义 |
|---|---|
| bit 0–2 | priority 等（boot_a=7，A 槽其它分区=4，boot_b=2） |
| **bit 3–5** | **retry count（0–7，每启动减 1）** |
| **bit 6** | **successful（置 1 后不再递减）** |
| bit 7 | unbootable |
| bit 2 | active（qbootctl 的判断方式，实测一致） |

**③ `qbootctl -m` 只动 success 位，GPT 全部校验通过**

```bash
sgdisk --backup=/root/gpt-bak-<日期>/sdX.gpt /dev/sdX   # 先备份所有 LUN
/root/qbootctl-src/build/qbootctl -m
# SLOT _a: Marked boot successful
sgdisk --verify /dev/sda /dev/sdb /dev/sdc /dev/sdd /dev/sde /dev/sdf   # 全部 ok
python3 /usr/local/bin/gpt_slot_attrs.py /dev/sde | grep boot_
# boot_a 0x002f... -> 0x006f...（仅 bit6 置位，retry 位保持 5）
# 同时 14 个 A 槽分区（abl_a/aop_a/boot_a/dtb_a/modem_a/tz_a/vbmeta_a…）被置 success
```

**④ 连续 4 次重启验证**

```
每次启动后：qbootctl -> SLOT _a: Active 1 / Successful 1 / Bootable 1
            boot_a 属性保持 0x006f...（success=1，retry 位不再递减）
            logfs 中 "retry count" 行不再出现
            j716f-mark-success.service = active（"already marked successful"）
```

**⑤ 已落地到设备的改动**

| 项 | 位置 |
|---|---|
| qbootctl 二进制 | `/usr/local/sbin/qbootctl`（源码 `/root/qbootctl-src`，可重新编译） |
| 开机标记服务 | `/etc/systemd/system/j716f-mark-success.service`（已 enable） |
| GPT 备份 | `/root/gpt-bak-MMDD/{sda..sdf}.gpt`（`sgdisk --backup`，恢复用 `--load-backup`） |
| 诊断工具 | `/usr/local/bin/gpt_slot_attrs.py`、`/usr/local/bin/slot-attr-dump.sh` |
| gdisk | `pacman -S gdisk`（sgdisk 备份/校验） |

**⑥ 注意事项**

- **刷写 boot_a/boot_b 后**：ABL 会按 Android 规范清除 success 标志并重置 retry count，**下一次启动服务会自动重新标记**，无需人工干预。
- **`qbootctl -s`（切槽）没有验证**：本机布局下它会把 byte54 写成 `0x0F`，会**覆盖 retry 位**（把计数变成 1），有耗尽风险。**暂时不要用 `-s` 切槽**；需要切槽时先备份 GPT、单独实测，或用 PC `fastboot set_active`。
- 如果系统连续 7 次根本起不来（连 Linux 都进不去），success 位无法被设置，仍可能耗尽 → 这时靠 §5 的恢复/兜底路径（mini-recovery 见 docs/84）。
- 服务在 multi-user 之后执行；早于它的启动计数仍可能被 ABL 递减，但因为 success 位是持久的，正常情况下不会发生。

---

## 4. 如果 qbootctl 布局不匹配（备选方案）

现象：`qbootctl` 把当前 active 槽读成 0，或 `-m` 后 fastboot 侧 `slot-successful` 不变。

1. 用 `fastboot set_active b` → 回 Linux dump GPT → 再 `fastboot set_active a` → 回 Linux dump GPT；两次 dump 用
   `python tools/gpt_slot_attrs.py --diff dir1 dir2` 对比，找出 active 位。
2. 在两次重启之间 dump，找出 retry 递减的位（每次只变几个 bit）。
3. 按旧版布局写一个最小工具（只置 successful 位，重算 GPT CRC32，主/备表同步，支持 4096 字节扇区）；或者直接改 `qbootctl` 的 `AB_FLAG_OFFSET`/位常量重新编译。
4. 无论哪种，都先在 **boot_a 的单个分区项** 上验证写入 + CRC 正确（`sgdisk --verify`），再全槽写。

> 写 GPT 的关键点：主表和备份表都要改；分区项数组 CRC32 与 header CRC32 都要重算；写前必须确认扇区大小（本机 LUN 是 4096，不是 512）。这也是优先用 qbootctl 而不是手写脚本的原因。

---

## 5. 兜底层（即使 ABL 计数真的无解，也尽量不接电脑）

按"代价从低到高"排列：

| 方案 | 做法 | 需要什么 | 备注 |
|---|---|---|---|
| **A. recovery 自修（推荐，已跑通）** | mini-recovery 装进 `recovery_a/b`；系统内 `tools/write_bcb.py --command boot-recovery` 重启进 recovery；recovery 从 SD 卡 payload 重刷 boot/rootfs 并用 `qbootctl -m` 标记成功 | 无（设备自身） | **BCB 路径已实测可用**（docs/84 §4.7）；recovery 会清 BCB，不会循环 |
| **B. 安卓手机当 fastboot 主机** | 手机 OTG 连平板，装 Bugjaeger（支持 fastboot）或 Termux + `fastboot`，执行 `fastboot set_active a && fastboot reboot` | 一部安卓手机 + OTG 线 | 大部分人有手机；比电脑现实 |
| **C. USB 小棒自动救** | RP2040/ESP32-S3 + USB-C 做成 fastboot host，检测到平板 fastboot 后自动发 `set_active:a` + `reboot` | 一个 20–40 元的小棒 | 可做成"随身救砖棒"，插上 5 秒解决 |
| **D. PC 看门狗** | 现有的 `tablet/fastboot-watchdog-v2.ps1` | 电脑 + USB 线 | 现状兜底，不理想 |

> **A 已验证**（2026-11-10：BCB→recovery→SD 卡 payload 刷写→回系统全部跑通，见 docs/84 §4.7）。所以"7 次计数"问题的最终形态是：正常时 qbootctl 每启动标记成功，根本不会耗尽；万一耗尽/被标 unbootable，也能在设备上自己进 recovery 重刷修好。完全可以不接电脑。

---

## 6. 与线刷/卡刷的关系

- `qbootctl -m` 让 **正常重启不再需要 PC**，这是发布包"免电脑"体验的前提（**已实测 + 已装机**）。
- **不要用 `qbootctl -s` 切槽**：本机布局下它写入的 `0x0F` 会覆盖 retry 位（变成 1），有耗尽风险。需要切槽时用 PC `fastboot set_active`（已知安全），或在备份 GPT 后单独实测 `-s`。卡刷/无 PC 更新不依赖切槽（见 docs/84：双槽写同一镜像 + recovery）。
- 发布包里应内置 qbootctl 的预编译二进制（或直接预装服务），别让最终用户自己编译。

---

## 7. 风险与待实测项

| 风险/未知 | 说明 | 缓解 |
|---|---|---|
| 写 GPT 损坏分区表 | 最严重可到"设备无法引导" | 已先 `sgdisk --backup`（/root/gpt-bak-*）；本次 `-m` 后 `sgdisk --verify` 全 OK |
| 布局与 qbootctl 假设的差异 | 本机已实测：active=bit2、success=bit6、retry=bits3-5 | `-m` 只置 bit6，安全；**不要用 `-s`** |
| `qbootctl -s` 会覆盖 retry 位 | 实测发现其写的 `0x0F` 会把 retry 变成 1 | 暂不使用 `-s`；切槽走 fastboot 或备份后单独实测 |
| qbootctl 的 `get_number_slots` 要求 `boot_*` 成对存在 | 本机 boot_a/boot_b 都存在，OK | — |
| 系统本身起不来（内核坏）时仍可能耗尽 | 计数只在"能进系统"后才被标记成功 | boot_a/boot_b 保持稳定镜像 + recovery 兜底（docs/84） |
| 服务启动太早/太晚 | 必须在 multi-user 之后；失败要可在 journal 看到 | 单元里 `WantedBy=multi-user.target`，日志进 journal |
| 按键进 recovery 是否可用 | Lenovo 组合键未知 | 优先用 BCB command（软件触发），不依赖按键 |

**已完成（2026-11-10）**

- [x] `sgdisk --verify` 所有 LUN 通过，GPT 备份完成（`/root/gpt-bak-*`）
- [x] `tools/gpt_slot_attrs.py` 能读当前 GPT；属性位与 ABL 日志对得上（retry 6→5）
- [x] qbootctl 构建成功、dump 正确（Active 1 / Successful 0）
- [x] `qbootctl -m` 成功，`sgdisk --verify` 全 OK，success 位写入 14 个 A 槽分区
- [x] 连续 4 次重启：success 保持、retry 不再递减、ABL 日志不再出现 retry 行
- [x] 服务 `j716f-mark-success.service` 已 enable，每启动自动 `-m`
- [ ] 10 次以上连续重启（继续观察即可，目前已 4 次）
- [x] `write_bcb.py --command boot-recovery` 能进 recovery（docs/84 §4.7，已验证）
- [ ] recovery 内修复 unbootable 的流程（需要构造 unbootable 场景，未测）
- [ ] `qbootctl -s` 切槽行为（**未测且有风险，暂不使用**）
