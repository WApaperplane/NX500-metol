# 快照重建：路径裁决与执行前置（2026-11-27）

> 触发：用户接受「重建快照」这一代价。
> 本文取代 `EYE_VLOG_METOL_COEXIST_2026-11-27.md` §2/§8 里「自造 `modstack.sh` 事务器」的假设。
> 结论先行：**不要自造。上游 0.4.0 R1 已经是一台真机、同一条流程的完整实现，而且维护者签过合并测试。**

> **2026-11-28 勘误（推翻本文三处论断，§1.3 / §3 末 / §5 已就地改写）**
>
> 1. **「上游不重写 ExecStart、已放弃 launch.sh」是误读。** 上游是**两段并存**：制作快照那次启动走 `68-nxl-full040.conf` → `ExecStart=/bin/sh /usr/local/lib/nxl-full040/launch.sh`，`worker.sh:335-337` 把"launch.sh 被选中"当**硬门**（`ROOT_LAUNCHER_NOT_SELECTED`）；恢复后走 `hib_post` → `nxl-full040-resume.service` → `wake.sh`。两句 README 讲的是恢复段，不能推出制作段不用 launcher。
> 2. **「`make_snapshot.sh` 不传参 = 语法错 = `set -e` 退出」不成立。** `[ $1 = "pcache" ]` 在 `if` 条件位，条件失败不受 `set -e` 管辖；空参时 `[` 打一行 `unary operator expected`、rc=2、进 `else`，继续 `sync` 与 `reboot -f`。已用 bash/sh 实测。
> 3. **`probe-rebuild.sh` 取不到 `P9_MD5`。** 原 `m()` 用 `[ -f ]` 判块设备，恒假 ⇒ 恒 `MISSING`。已改 `[ -b ] || [ -f ]`。⇒ §7 里 `P9_MD5` 这一项在改前从未被真正验证过。

---

## 0 裁决

| 问题 | 裁决 | 依据 |
|---|---|---|
| 重建能不能绕过 | 不能 | 恢复路径不 exec，LD_PRELOAD 只能落在制作那次启动 |
| 注入路线怎么走 | **两段并存**：制作段 `launch.sh`，恢复段 `wake.sh` | 上游 `worker.sh:333-337` 把 launch.sh 当选装硬门；`rebuild/wake.sh` 管恢复（11-28 纠正，见 §3/§5） |
| 谁来重建 | 原厂 rescue 路径（`systemctl rescue`） | 上游 0.4.0 `rebuild/README_zh.md`：「沿用社区已使用的原厂 rescue 重建路径。本包不会另行调用先擦除 80 MiB 的原厂独立维护命令」 |
| 要不要写 `modstack.sh` | **不写** | 上游 `stage-postflash.py`（CHECK/CONTROL/EYE/CLEAR/INSPECT/REBUILD-*）+ `coexist_rebuild.py`（PLAN/PREPARE/STATUS）已经是安装器框架 |
| 观测通道用什么 | **SD 卡**，不用 WiFi/SSH | 上游 `readout` 全部落 `$SD/nxl-full040` 与 `NXDEMO51/**/resume/`；11-26 断网即失明 |

---

## 1 三处纠正（11-27 自纠；其中 1.3 的 `set -e` 论断已于 11-28 二次纠正）

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

两个陷阱（**第 1 条 2026-11-28 已推翻，见下**）：

- ~~**不传参数会死在第 4 行**：`[ $1 = "pcache" ]` 在 `$1` 为空时展开成 `[ = "pcache" ]` → 语法错 → `set -e` 直接退出，`reboot -f` 永远到不了。~~
  **纠正**：它在 `if` 的**条件位**，`set -e` 根本不适用于条件表达式。空参时 `[` 只是报 `unary operator expected` 并返回 2，于是走 `else` → `sync` → `reboot -f` 照常执行。bash 与 dash 实测一致。
  真实后果不是"退不出"，而是**静默少做一步**：不传 `pcache` 就不设 `APPPREF_FW_UPGRADE_CHECK`，reboot 照样发生，看起来像成功。
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
| `fast-start/nxl-full040/launch.sh` + `capture.conf`（1.0KB / 76B） | **制作段入口**：`68-nxl-full040.conf` 把 `ExecStart=` 换成 `launch.sh`，`eligible()` 命中才 `LD_PRELOAD exec`，否则裸 `exec` 原厂 App | `metol/launch.sh` + 同类 drop-in |
| `rebuild/wake.sh` + `nxl-full040-resume.service` | **恢复段入口**：`hib_post.service` → resume unit → `wake.sh`，在**已恢复的进程**里验身份并 enable 人眼 + 起菜单监听 | `metol/wake.sh` + `nx-eye-resume.service` |
| `rebuild/test_rebuild.py`（21KB） | 主机回放 | 我们的 `test_server/extensions/` |
| `rebuild/README_zh.md` | 逐步流程 + 两支判断 + 验收判据 | 本文件 |

**统一入口是两段，不是一段（2026-11-28 纠正）**：本文原写「它**不重写 `di-camera-app.service` 的 `ExecStart`**，而是走 `hib_post.service → nxl-full040-resume.service → wake.sh`」。**前半句错。**

代码事实：

```sh
# fast-start/nxl-full040/capture.conf
[Service]
ExecStart=
ExecStart=/bin/sh /usr/local/lib/nxl-full040/launch.sh

# fast-start/nxl-full040/worker.sh:333-337
mkdir -p "$DROP" || fail SERVICE_DIRECTORY_CREATE
install_new "$PKG/capture.conf" "$CONF" 644 || fail CAPTURE_OVERRIDE_INSTALL
"$SYSTEMCTL" daemon-reload && "$SYSTEMCTL" show di-camera-app.service -p ExecStart > "$REPORT/service.txt" &&
    grep -Fq '/usr/local/lib/nxl-full040/launch.sh' "$REPORT/service.txt" || fail ROOT_LAUNCHER_NOT_SELECTED
```

上游**装 drop-in、`daemon-reload`、再断言 `ExecStart` 里就是 `launch.sh`**，不符即 `fail`。所以「上游已放弃 `launch.sh`」不成立，P4 的迁移依据也随之作废。

两段的分工：

| | 制作快照那次启动 | 恢复后的普通启动 |
|---|---|---|
| 入口 | `launch.sh`（常驻 drop-in） | `hib_post` → `resume.service` → `wake.sh` |
| 动作 | `eligible()` 命中 ⇒ `LD_PRELOAD` 加载 Eye 库 exec App | 在**已在跑**的进程里验 `camera_pid`/`launch-claim`，再 enable |
| 命不中怎么退 | 裸 `exec "$APP"`（行为退化成原厂） | `stop <REASON>` 写状态后 `exit 1` |
| 能否撤回 | **不撤回**，常驻 | 无需撤回 |

关键设计点：**安全性来自"退化"，不是"来自"撤回"。** drop-in 一直挂着，`eligible()` 不成立就直通原厂 App，所以常驻没有副作用。我们 `arm-rescue.sh:31` 那句

```sh
systemctl show di-camera-app.service -p ExecStart | grep -Fq '/bin/sh /usr/local/lib/nx-eye-af/launch.sh' || fail WRONG_LAUNCHER
```

与上游**同构**（它也是"不是我的 launcher 就 fail"），不是冲突。**§5 的裁决因此反转。**

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

## 5 裁决（2026-11-28 反转）：`launch.sh` 路线不退

原节标题是「待裁决：我们的 `launch.sh` 路线要不要退」。前提已被 §3 推翻——上游没有退，反而把它当硬门。⇒ **裁决：不退，按上游的两段骨架接线。**

| | 我们（`contrib/nx500-eye-af/metol/`） | 上游 0.4.0 |
|---|---|---|
| 制作段注入点 | drop-in `ExecStart=` → `launch.sh` | **相同**（`68-nxl-full040.conf`） |
| 恢复段入口 | `nx-eye-resume.service` → `wake.sh` | `nxl-full040-resume.service` → `wake.sh` |
| 改 unit 的前提 | `remount,rw /` | **相同**（`unprotect_root` / `reprotect_root`） |
| 刷机后 | drop-in 随根分区一起没了，需重装 | **相同** |
| 运行时自校验 | `app` sha256 ∈{`55dcc781…`,`ec395123…`} + `verify-payload.sh` 整包校验 | 只靠安装期 `pin` 逐文件 sha256 + `claim.arm` 原子性，运行时不算 |
| 门控条数 | 8 条 + 上述两条 | 6 条（无 app hash、无 verify-payload） |

**差异只剩"我们更严"，没有"方向不同"。** 我们那两条多出来的校验是净增益（App 被换过就不 preload），不构成退路线的理由。

### 5.1 唯一要保留的批评：时序是假设，不是保证

`eligible()` 只能在 **exec 那一刻**生效；而 `After=hib_ready.service` 是**空引用**（该 unit 在机上不存在，见 `camera-startup-details.txt:214`）。所以「App 一定在 p9 备好之后才 exec」**没有 systemd 保证**，只能靠 `/proc/swaps` 里 p9 是否在 swap 上事后判断，判不中就**静默不 preload**。

**归属要改**：这不是"我们的设计缺陷",而是**这条路线固有的时序假设，上游用完全相同的手法承担同一风险**（`launch.sh:10` 的 `awk '$1=="/dev/mmcblk0p9"'`）。既然裁决采纳上游骨架，这条假设就一并继承，必须在 P5 显式验证（见 §6），而不是当成待修 bug。

---

## 6 执行顺序（每步都是判据，不是仪式）

| 步 | 动作 | 通过判据 | 失败即停 |
|---|---|---|---|
| P1 | 跑 `contrib/nx500-eye-af/capture/probe-rebuild.sh`（只读） | 15 项全部符合预期，见 §7 | 任一项不符 |
| P2 | 恢复 mod 基线（刷机后 `/opt/usr` 全空） | Log117 引擎 `eaa49f88…`、`vlog.sh status` 有 lease | — |
| P3 | 备 Recovery 网：FAT32 卡 + 根目录 `nx500.bin`（`df08b6f8…`） | `GetVolumeInformation` 报 FAT32（**不是 exFAT**） | 卡是 exFAT |
| P4 | **按上游两段骨架接线**：制作段 `capture.conf`→`launch.sh`，恢复段 `hib_post`→`wake.sh`（§5 已反转，不再"退 launch.sh"） | 主机回放通过 + `systemctl show -p ExecStart` 含本包 `launch.sh` | — |
| P5 | dry rebuild：不 arm、不 preload，只验证两件事——① 制作那次启动 exec 的就是 `launch.sh`；② `eligible()` 未命中时它裸 exec 原厂 App（时序假设的实测，见 §5.1） | 卡上出现带 `bootmode=` 的日志行；取景正常且 `/tmp/nxui.log` 无 preload 痕迹 | 无日志 ⇒ 上游路线也不成立，回 §5 |
| P6 | 正式捕获（上游 §4 流程） | §4 七条 | 停在原地，不断电 |

**P1 现在就能跑**（相机醒机后一条命令）。P3 必须在 P5 之前——p9 一旦被动过，回到原状的唯一通路就是 Recovery 卡。

---

## 7 P1 判据表

| KEY | 期望 | 不符的后果 |
|---|---|---|
| `BOOTMODE` | `2` | 不是 2 说明上次没干净恢复 |
| `ETC_SNAPSHOT` | `present` | `missing` ⇒ `hibernation.sh` 会走早退分支，制作那次不备 p9 ⇒ 重建必失败 |
| `P9_SIZE_SECTORS` | `307199` | 分区变了，备份不适配 |
| `P9_MD5` | `8a669a6a1b503d541a7f4a463d49ca5e` | 不是 ⇒ p9 已被动过，先查是谁。**注**：11-27 版探针用 `-f` 判块设备 ⇒ 该项恒 `MISSING`，从未真正比对过；已修，本轮是首次实际取值 |
| `P9_IN_SWAPS` | `no` | `yes` ⇒ 上次制作没走完，卡在半态 |
| `ROOT_LINE` | 含 `ro` | 可 rw ⇒ 有人 remount 过没还原 |
| `UNIT_FILE_SHA` | 记录基线 | 刷机后应回到原厂值 |
| `UNIT_EXECSTART` | 原厂裸路径 | **已是 `launch.sh` 不再必然是异常**（上游常驻 drop-in，见 §3）：要用 `head -1 $DROP/68-nxl-full040.conf` 分辨是本包落的还是 `nx-eye-af` 落的残留 |
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
