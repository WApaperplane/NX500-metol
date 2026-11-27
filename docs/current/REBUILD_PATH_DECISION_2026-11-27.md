# 快照重建：路径裁决与执行前置（2026-11-27）

> 触发：用户接受「重建快照」这一代价。
> 本文取代 `EYE_VLOG_METOL_COEXIST_2026-11-27.md` §2/§8 里「自造 `modstack.sh` 事务器」的假设。
> 结论先行：**不要自造。上游 0.4.0 R1 已经是一台真机、同一条流程的完整实现，而且维护者签过合并测试。**

---

## 0 裁决

| 问题 | 裁决 | 依据 |
|---|---|---|
| 重建能不能绕过 | 不能 | 恢复路径不 exec，LD_PRELOAD 只能落在制作那次启动 |
| 谁来重建 | 原厂 rescue 路径（`systemctl rescue`） | 上游 0.4.0 `rebuild/README_zh.md`：「沿用社区已使用的原厂 rescue 重建路径。本包不会另行调用先擦除 80 MiB 的原厂独立维护命令」 |
| 要不要写 `modstack.sh` | **不写** | 上游 `stage-postflash.py`（CHECK/CONTROL/EYE/CLEAR/INSPECT/REBUILD-*）+ `coexist_rebuild.py`（PLAN/PREPARE/STATUS）已经是安装器框架 |
| 观测通道用什么 | **SD 卡**，不用 WiFi/SSH | 上游 `readout` 全部落 `$SD/nxl-full040` 与 `NXDEMO51/**/resume/`；11-26 断网即失明 |

---

## 1 三处纠正（我上一轮说错或漏掉的）

### 1.1 重建写入器不是 `hib_post`

11-26 我在收尾时写「rebuild trigger = `hib_post.path` → `hib_post.service` → `/usr/bin/hibernation.sh`」。**错。**

现场证据（`rea-evidence/20261126-eye-log117/camera-startup-details.txt:205-209`）：

```
[Path]
PathExists=/tmp/.hib_post
#!/bin/bash
if [ -x /opt/usr/nx-ks/init.sh ]; then
  /opt/usr/nx-ks/init.sh
fi
```

`hib_post` = **metol 的开机钩子**（boot+37.8s 跑 `init.sh`）。而 tar 里原厂 `hib_post.service` 是 `/bin/sh -c "/sbin/swapoff -a"` —— NX-KS 把这个 unit 换掉了。两者不是一回事。

### 1.2 `hibernation.sh` 不做重建，它只做「备好 p9」

`usr/bin/hibernation.sh`（sha256 `dd3687d3…`）全文只有四段动作：

1. `bootmode == 4` ⇒ 打印 `bootshell set to cold-boot` → `touch /tmp/.hib_ready` → **exit**
2. `/etc/snapshot` 不存在 ⇒ `touch /tmp/.hib_ready` → **exit**
3. 否则：`card` LED 置 `timer` → zram 50MiB → `mkswap -c /dev/mmcblk0p9` + `swapon` → `mount -o ro,remount /`
4. `touch /tmp/.hib_ready`

**它对 bootmode 1/2/3 没有任何区别对待。** 唯一的判据是 `/etc/snapshot` 存在与否。
⇒ 「bootmode 1 与 3 的差别」（旧 V1）不在这条链上，问它没有意义；真正要问的是「制作那次启动有没有 exec App」。

真实的镜像写入由 App 侧触发（脚本里 `#export HIB=a` ——「set environment variable to let app do hibernation」）。这也是 `launch.sh:eligible()` 卡 `[ -n "${HIB:-}" ]` 的原因。

### 1.3 原厂的「擦 80MiB」不是重建，是维护

`usr/bin/make_snapshot.sh`（sha256 `08df6e37…`）：

```sh
if [ -e /etc/snapshot ]; then dd if=/dev/zero of=/dev/mmcblk0p9 bs=1M count=80
else /bin/touch /etc/snapshot; fi
if [ $1 = "pcache" ]; then … prefman set … APPPREF_FW_UPGRADE_CHECK l 1 …; else sync; fi
reboot -f
```

两个陷阱：

- **不传参数会死在第 4 行**：`[ $1 = "pcache" ]` 在 `$1` 为空时展开成 `[ = "pcache" ]` → 语法错 → `set -e` 直接退出，`reboot -f` 永远到不了。
- `/etc/snapshot` 不存在时它**只建标记不擦盘**。所以「跑一遍」和「重建」不是同义词。

而守卫（`verify-before-capture.sh` / `protected_writers.py`）**明确拒绝** 这 80MiB 擦除。上游也明说不调用它。⇒ 这条路我们不走。

---

## 2 11-26 那次实际发生了什么（复盘）

时间线（`.ai-memory/20261126/eye-log117-handoff.md`）：

| 时刻(北京) | 事件 |
|---|---|
| 20:12:10 | dispatch 启动，`phase=WAITING_FOR_WIRELESS_OFF` |
| — | 用户关无线 ⇒ SSH 超时 |
| 第一轮 | 用户报「灯灭 + 正常取景」；日志：`arm-rescue` 在 `cmp` 缺失处退出，`p9` md5 与 `boot_id` 均未变，armed 归档 `rejected-missing-cmp` |
| 第二轮 20:22:37 | 修 `cmp` 后重启 dispatch；用户报「画面无 UI、卡灯未闪烁」，SSH 超时 |
| 20:27:56 | 满 5 分钟，指导取电池 10s 装回、继续等 4 分钟 |
| — | 再 OFF / 电池 5s / 普通启动 ⇒ **无法正常启动** |
| — | 进 Recovery Mode，报「SD 内无固件」（卡为 exFAT）⇒ 最终刷 1.12 |

三个判断：

1. **第二轮「无 UI + SSH 超时」不是失败证据。** `rescue.target` 本来就没有 GUI；而无线是**按流程要求关掉的**，所以 SSH 必然超时。这一轮的真实状态我们从未观测到——按今天的证据看，它很可能就停在 rescue.target 里正常等待。
2. **致命动作是之后的电池序列**：在 rescue 隔离态下反复断电重启，而不是「先读日志再决定」。
3. **而上游记录的恢复手段当时根本没试**：`0.4.0/eye-af/INSTALL.md` 明写 ——
   > Once the camera is fully off and card access has stopped, remove the mod SD and start once without it. This returned the original UI on the test unit after an earlier failed rebuild.
   > 中文速查同段：「整机断电后拔掉 mod SD，不带卡单启动一次」曾在一台测试机上救回原厂界面。

   我们直接从「起不来」跳到 Recovery + 刷机。

**结论：那次失败的主因不是 rescue 路径坏了，是我们把唯一的观测通道（网络）自己关掉，然后在无观测状态下做了最不该做的动作。**

---

## 3 上游 0.4.0 R1 已经有的东西（不用重造）

路径：`E:/NXWA/_ext_unified_r1/release/0.4.0/`

| 上游件 | 作用 | 我们的对应物 |
|---|---|---|
| `stage-postflash.py`（12.6KB） | 卡端安装器：CHECK / CONTROL / EYE / CLEAR / INSPECT / REBUILD-CHECK / REBUILD / REBUILD-PLAN / REBUILD-STATUS | 无 —— 我原本打算写的 `modstack.sh` |
| `coexist_rebuild.py`（12.3KB） | **共存型**重建：校验包 → 把旧 `nx-*`/`nxl-*`/NXDEMO51/旧 `NX500.bin`/旧入口**整项归档**（不删）→ 只留 `install.sh`/`nx_cs.adj`/`info.tg` 三个根触发 → `PRESERVE` dcim/albm/config/system/looks | 无 |
| `fast-start/nxl-full040/worker.sh`（21KB） | 机端主体：识别同代、备份并**逐字节比较整个 150MiB 快照**、存配置、装统一入口、前置检查 | 我们手搓的 `arm-rescue.sh` |
| `fast-start/nxl-full040/readout`（8.1KB）+ `fileops.arm` | 观测全部落 SD（`$SD/nxl-full040`、`NXDEMO51/**/resume/`），带 sha256 pin 与断点续跑 | 无 —— 我们用 SSH |
| `rebuild/wake.sh` + `nxl-full040-resume.service` | 统一入口：`hib_post.service` → resume unit → `wake.sh` 一次完成「验恢复进程 + 开人眼 + 起菜单监听」 | `metol/wake.sh` + `nx-eye-resume.service` |
| `rebuild/test_rebuild.py`（21KB） | 主机回放 | 我们的 `test_server/extensions/` |
| `rebuild/README_zh.md` | 逐步流程 + 两支判断 + 验收判据 | 本文件 |

**统一入口的关键设计**：它**不重写 `di-camera-app.service` 的 `ExecStart`**，而是走 `hib_post.service → nxl-full040-resume.service → wake.sh`。这与我们 `arm-rescue.sh` 里那句

```sh
systemctl show di-camera-app.service -p ExecStart | grep -Fq '/bin/sh /usr/local/lib/nx-eye-af/launch.sh' || fail WRONG_LAUNCHER
```

是**冲突的**：我们要求改写 ExecStart，上游要求不改写。上游 README 原话：「统一入口为原厂 `hib_post.service` → `nxl-full040-resume.service` → 根分区 `wake.sh`」，且「运行不依赖 SD 挂载回调」。

⇒ 我们那套 `launch.sh` + drop-in `ExecStart=` 的路线，是**在上游已经放弃的思路上做的**。这条要重新裁决，见 §5.

---

## 4 上游验收判据（比我们自拟的 A1-A6 硬）

`NXDEMO51/FAST_START_EYE_AF_ROOTLIB_*/resume/` 下：

| 文件 | 判据 |
|---|---|
| `normal-start.txt` | `EYE_AND_MENU_READY` |
| `full-menu.txt` | `LISTENER_INPUTS_OPEN` |
| `runtime.log` | `same_capture_process=1`、`ADAPTER_ENABLED_AFTER_RESTORE` |
| `eye-maps.txt` | 含 `nxl-eye-af*.so`，inode 与文件一致 |
| `eye-af.status` | `schema=28` |
| 人眼计数 | 增长 |
| 制作回执 | 两份 `decision=PASS` |

并且明说：**「安装登记成功、电脑回放通过均不能代替普通冷启动实机验收。」**

另有一条硬约束：**机身哈希工具有 128 MiB 上限**，150MiB 的 p9 自身 sha256 只能电脑端算（所以上游有 `fileops.arm` + 主机 STATUS 两条腿）。我们 `arm-rescue.sh` 里 `$bb md5sum /dev/mmcblk0p9` 能过是因为 md5sum 那份实现没这个限制，但**不能据此以为 sha256 也行**。

---

## 5 待裁决：我们的 `launch.sh` 路线要不要退

| | 我们（`contrib/nx500-eye-af/metol/`） | 上游 0.4.0 |
|---|---|---|
| 注入点 | 改写 `di-camera-app.service` `ExecStart=` → `launch.sh` | 不动 unit；`hib_post.service` → `wake.sh` |
| preload 时机 | App exec 时（制作那次启动） | 恢复后由 `wake.sh` 介入 |
| 前提 | 需要 `mount -o remount,rw /` 改 unit（P5 已验证可行） | 只需 `/opt/usr` + 根分区放一个 `wake.sh` |
| 风险 | 改的是原厂 unit，刷机即失效；且 `arm-rescue.sh` 把它当硬前置 | 上游已真机跑通 |

`launch.sh` 的 `eligible()` 还额外卡了 8 条（bootmode ∈{1,3}、`armed` 两行、boot_id 变化、p9 在 `/proc/swaps`、app hash ∈{55dcc781, ec395123}、`verify-payload.sh`、`HIB` 非空、`claim.arm` 原子写）。这套门很紧，但它**只能在 exec 时生效**，而 `After=hib_ready.service` 是**空引用**（该 unit 在机上不存在，见 `camera-startup-details.txt:214`），所以「App 一定在 p9 换好之后才 exec」这件事**没有 systemd 保证**，`launch.sh` 只能靠 `/proc/swaps` 判据事后放弃——**表现为静默不 preload，而不是报错**。这是设计缺陷，不是实现 bug。

---

## 6 执行顺序（每步都是判据，不是仪式）

| 步 | 动作 | 通过判据 | 失败即停 |
|---|---|---|---|
| P1 | 跑 `contrib/nx500-eye-af/capture/probe-rebuild.sh`（只读） | 15 项全部符合预期，见 §7 | 任一项不符 |
| P2 | 恢复 mod 基线（刷机后 `/opt/usr` 全空） | Log117 引擎 `eaa49f88…`、`vlog.sh status` 有 lease | — |
| P3 | 备 Recovery 网：FAT32 卡 + 根目录 `nx500.bin`（`df08b6f8…`） | `GetVolumeInformation` 报 FAT32（**不是 exFAT**） | 卡是 exFAT |
| P4 | 退 `launch.sh` 路线，改上游 `wake.sh` 路线（§5） | 主机回放通过 | — |
| P5 | dry rebuild：不 arm、不 preload，只验证「制作那次启动 exec 了 App」 | 卡上出现带 `bootmode=` 的日志行 | 无日志 ⇒ 上游路线也不成立，回 §5 |
| P6 | 正式捕获（上游 §4 流程） | §4 七条 | 停在原地，不断电 |

**P1 现在就能跑**（相机醒机后一条命令）。P3 必须在 P5 之前——p9 一旦被动过，回到原状的唯一通路就是 Recovery 卡。

---

## 7 P1 判据表

| KEY | 期望 | 不符的后果 |
|---|---|---|
| `BOOTMODE` | `2` | 不是 2 说明上次没干净恢复 |
| `ETC_SNAPSHOT` | `present` | `missing` ⇒ `hibernation.sh` 会走早退分支，制作那次不备 p9 ⇒ 重建必失败 |
| `P9_SIZE_SECTORS` | `307199` | 分区变了，备份不适配 |
| `P9_MD5` | `8a669a6a1b503d541a7f4a463d49ca5e` | 不是 ⇒ p9 已被动过，先查是谁 |
| `P9_IN_SWAPS` | `no` | `yes` ⇒ 上次制作没走完，卡在半态 |
| `ROOT_LINE` | 含 `ro` | 可 rw ⇒ 有人 remount 过没还原 |
| `UNIT_FILE_SHA` | 记录基线 | 刷机后应回到原厂值 |
| `UNIT_EXECSTART` | 原厂裸路径 | 已是 `launch.sh` ⇒ 上次没撤回 |
| `T2_PREMOUNT` / `T2_C2_SHA` | `present` / `ec395123…` | 缺失 ⇒ P5 注入是空跑 |
| `HIB_READY_UNIT` | `missing` | **出现** ⇒ 我关于空引用的判断要推翻 |
| `HIB_IN_TIZEN_ENV` | `0 或 1` | 若为 0 且制作那次也没有 ⇒ `launch.sh` 永不 eligible |
| `RESCUE_TARGET` | `present` | 缺失 ⇒ 上游路线不可用 |
| `EYE_STATE` | `missing`（刷机后） | 有残留 ⇒ 先 withdraw |
| `EYE_ARMED` | `missing` | 有 ⇒ 必须先归档，否则重复 rescue |
| `SD_INFO_TG` / `SD_NX_CS` | 按本轮需要 | 缺 ⇒ 卡触发链不完整 |

---

## 8 红线

1. **制作期间不断电、不拔电池、不拔卡。** 上游：「A timer expiring by itself is insufficient grounds to interrupt it.」
2. **不关无线去赌 SSH**。观测走卡（`readout`），不走进程网络。
3. **起不来时先试「拔掉 mod SD，不带卡单启动一次」，再考虑 Recovery。** 11-26 跳过了这一步，代价是一次刷机。
4. **`make_snapshot.sh` 不碰**（守卫本来就拒）。
5. **不把别台机身的 p9 写进本机**（上游明示）。
6. 主机回放通过 ≠ 实机通过。
