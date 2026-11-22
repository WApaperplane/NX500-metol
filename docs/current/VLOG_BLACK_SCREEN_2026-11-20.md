# V-Log 录像黑屏 / 菜单"不可用"：根因与恢复（2026-11-20）

> 症状：开 V-Log 后录像 → 取景黑屏；此后主菜单里的「V-Log 开关 / 回原厂」
> 按了没反应（"不可用"）；重启也不一定回来。

## 1. 黑屏不是"效果不对"，是三个状态互相打架

事故现场（`/tmp/nx-lut-lite` 全量取证）：

| 层 | 实际值 | 应该是 |
|---|---|---|
| `clean-log.policy` | `NXL_CLEAN_LOG1 READY` | 没开 log 时应**不存在** |
| A18 槽（`0x81119F00`） | md5 `235ad453…` | 原厂 `4c0cc78c…`（= `session/a18_orig.bin`） |
| `session/video_enabled` | 存在 | 只应在"视频试用"期间存在 |
| DYN24（24 条视频选择器） | **24 条全 = `0x81119f00`** | 24 个各不相同的原厂地址 |
| gamma | `mode=FACTORY phase=OFF` | 与 policy 不一致 |

DYN24 是录像通路的表选择器。`log_neutral` 末尾的 `video_enable()`
（`nx_regtabwrite.arm dyn-write 0x81119F00`）把**整张 24 条表重指到 A18** ——
RC5 自己的日志也写了 `OK VIDEO_TRIAL_ON; start a NEW recording to test`。
A18 里此时是 log 的参考表（4913×4B 的 3D 表），而 DYN24 原本各是 1D 曲线，
形状对不上 ⇒ 录像启流时读空 ⇒ 黑屏。

RC5 的 JSON 自己就承认没验证过这条路：
`actual_new_video_consumption_verified:false`、`native_queue_is_hardware_completion:false`、
`hardware_pixels_verified:false`。

> **★ 2026-11-20 晚些时候的更正**：上面这条"形状对不上"的推断**被证伪了**。
> 24 个槽读回来全部是 19,712 B 的同格式 3D LUT（见
> [DYN24_MAP_2026-11-20.md](DYN24_MAP_2026-11-20.md)），而且 11-07 我们已经把
> 23 项全指 A18、录像 179 帧正常。**DYN24 全指 A18 解释不了黑屏。**
> 现在最可疑的是 gamma native 事务（`native_queue_is_hardware_completion:false`
> —— 排队就走，不等硬件确认），用 §5 的 A/B 实验定性。

## 2. 为什么菜单里"不可用"

`vlog.sh toggle` 见到 `clean-log.policy` 就只走 `restore`，而 `restore` 死在这里：

```
reason=VIDEO_AUDIT_DIRECTORY_FAILED
```

`nxl-gamma-reload.arm` 往 `/mnt/mmc/NXDEMO51/` 写审计文件（二进制里写死
`"/mnt/mmc/NXDEMO51/GAMMA_R11Q_%llu_%d_%08x"`），而这个目录**默认不存在**。
于是 restore 直接退出，半状态原地留下 —— 之后每一次 toggle/off 都死在同一处，
表现为"按钮没反应"。

## 3. 恢复（黑屏时 ssh 里照抄）

```sh
mkdir -p /mnt/mmc/NXDEMO51                                   # 补审计目录
/opt/usr/nx-ks/gamma/bin/nx_regtabwrite.arm dyn-restore      # 视频选择器回 24 条原厂
/opt/usr/nx-ks/vlog.sh off                                   # A18 回 a18_orig + 清 policy
FILMLAB_PW=1 /opt/usr/nx-ks/filmlab.sh neutral               # PW 归中性（V-Log 前置）
/opt/usr/nx-ks/preflight.sh vlog                             # 应当 READY=1
```

实测恢复结果：A18 = `4c0cc78c…`（与 `a18_orig.bin` 逐字节相同）、policy 消失、
`video_enabled` 消失、DYN24 回到 24 个不同地址、`dyn-restore` 报 `READBACK ok`、
preflight `READY=1`。

## 4. 已做的硬化

| 改动 | 作用 |
|---|---|
| `vlog.sh::ensure_run` 同时 `mkdir -p /mnt/mmc/NXDEMO51` | restore 不会再因缺审计目录半途死掉 |
| 新增 `vlog.sh video-off` | 单独还原 DYN24，不碰 gamma/A18 —— 黑屏时的最小自救 |
| `restore` 成功后再查 `session/video_enabled`，残留就补一次 `dyn-restore` | 防"policy 没了但指针还指着 A18" |
| `vlog.sh on/toggle` 前检查 `tc.sh status` = `custom` 就拒绝 | TC 曲线与 V-Log 改同一条 gamma，不能叠 |
| `tc.sh set <非stock>` 前检查 `clean-log.policy` 就拒绝 | 反向互斥 |

实测：`tc.sh set log` → `vlog.sh on` → `BLOCKED=TC_CURVE_CUSTOM`；`tc.sh off` 回 `CURVE=stock`。

## 5. 仍未解决

- **黑屏的触发点还没指认**。DYN24 那条已被排除（见 §1 的更正），剩下最可疑的是
  gamma native 事务。二分实验：
  A）只跑 `lut.sh clean-log-prepare`（写 A18 + video_enable，不跑 gamma）；
  B）只跑 `gamma-reload.sh log`（不 prepare）。哪个黑，问题就在哪一半。
- **V-Log 录像本身仍然不成立**：即便黑屏修好，`video_enable` 那条
  "24 条全指 A18"的路也要重新评估（11-07 的 23 项版本录像正常，说明路本身不必然坏）。
  要真让录像带 log，得先弄清 DYN24 的 24 条各对应什么 —— 见
  [DYN24_MAP_2026-11-20.md](DYN24_MAP_2026-11-20.md)。
- 在此之前，V-Log 只当**拍照/取景**用；要录像先 `vlog.sh off`。
- `lut.sh video-off` 在 VLOG 模式下救不了：lut-core 的分支让它走 `log_neutral`，
  末尾又 `video_enable`。必须用 `vlog.sh video-off`（直接调 dyn-restore）。

## 6. 当日续：A/B/C 三分实验（结论：RC5 的 gamma 引擎退役）

`scripts/probe-black.sh` 把三条路径拆开跑，每步自恢复（`dyn-restore` → `vlog.sh off`
→ `filmlab neutral`），并在屏幕上弹字母标记（弹窗是独立 X 窗口，黑屏时仍可见）。

| 步 | 只做 | 结果 |
|---|---|---|
| A | `lut.sh clean-log-prepare`（写 A18 + 三张登记表 + video_enable） | 跑通；A18 `4c0cc78c` → 变 → 自动恢复回 `4c0cc78c` |
| B | `gamma-reload.sh log`（native gamma 事务） | 这次**成功**：`qualified=1 applies=1 queues=29 fault=0` |
| C | `vlog.sh toggle`（完整链） | 跑通，`VIDEO_POINTERS=restored` |

审计目录补上之后三条都能跑 —— 说明当年"菜单不可用 + 黑屏"就是缺目录造成的半状态。

**但是**：14:40 与 14:49 各有一次**整机重启**（uptime 归零、/tmp 清空），
都发生在 V-Log 往返之后。审计 JSON 里 `fault=0`、`transaction_fault=0`、
`state_consistent=true`，引擎自认为一切正常 —— 重启不归它管，它也不知道。

⇒ **结论：这台机器上任何"直接改 gamma"的路都不可靠**：
- RC5 的 native 事务：能成功，但会随机把整机搞重启（两次实测）；
- TC 曲线的 19 节点写入：`tc.sh set log` 现在会 `ERR=REFRESH_FAIL` 自己回滚；
- 唯一稳定的是**写 A18 3D LUT**（`lut.sh apply` → `OK instant switch`，录像/取景/照片同路径）。

## 7. 所以「LUT 与 V-Log 同时启用」的正确解法：烘成一张表

既然 gamma 动不得，就把 log 曲线**烘进 3D LUT**：一张表里同时包含 log 编码和风格，
写进 A18，走已验证的换表闭环。没有第二次写入，就没有不同步、没有半状态、不会重启。

`tools/lutgen/bake_log.py`：

```sh
python tools/lutgen/bake_log.py looks/fuji_classicchrome.bin -d out_vlog/     # k=20
python tools/lutgen/bake_log.py looks/x.bin -o vk_x.bin --log 10              # 更轻
```

- 曲线是解析式 `Y' = log(1+k·y)/log(1+k)`，k 默认 20（64→150，192→233）。
- **不要用 `scripts/tc/*.tcy` 来烘**：那 19 点是 TC 的"显示→场景"反 gamma 表
  （flat 在输入 64 处给 21），方向相反，套上去是加对比而不是 log。
- 输出仍是 19,712 B 的 `.bin`，直接丢进 `/mnt/mmc/looks/` 就是一条 look。

代价说清楚：8bit 的"假 log"——动态范围不会增加（11-07 已判视频无 10bit 地基），
但暗部被抬起来、高光被压住，后期拉得动，而且**录像不会黑**。

## 8. 实机配方（2026-11-20 用户实拍确认）

```
look : vk_fuji_soft         （k=6, ev=-0.25，白 255→214、中 128→153）
相机 : 曝光补偿 -1 EV
结果 : 曝光正常
```

为什么正好是 -1 EV：这张表把中调抬了约 +0.25 EV、又把 log 的暗部抬升算进去，
合计约 +0.5～0.6 EV；相机压 -1 EV 后净剩约 -0.4 EV —— **这正是 log 该有的样子**：
高光留出余量，后期拉回来时才不会死白。不是补偿过头，是 log 的正常拍法。

换 look 或换 `--log` 数值后，EV 要跟着重调。经验换算：表里每 0.1 EV ≈ 7 个码值。

若不想每次手动压 EV：烘的时候把 `--ev` 直接再降 1 档（-1.25），
这样 EV 回 0 也能直接用 —— 代价是这张表只适合这一种曝光习惯，换场景要重烘。

### 实拍验证（SAM_0489.MP4，2026-11-20 14:57）

| 项 | 值 |
|---|---|
| 编码 / 分辨率 / 帧率 | HEVC 2560×1440 yuvj420p 25 fps |
| 时长 / 大小 | 6.16 s / 37,908,722 B（moov 完整，ffprobe 直接读出时长） |
| 首 3 帧 | YAVG 122.69 / 122.71 / 122.75，YMAX 215–217，YMIN 0 |
| 末 3 帧 | YAVG 124.99 / 124.99 / 125.05，YMAX 213–214 |

⇒ **不黑（YAVG 122 而不是 0）、不死白（YMAX 217 而不是 250+）、首尾亮度一致
（差 <3）⇒ 录像过程中 LUT 没有掉表。** 画面内容与取景一致，无花屏/色块。

这条验证的作用：它证明「LUT 走 A18」那条路在录像里是稳的，
而 RC5 那条「改 gamma + 重指 DYN24」的路是黑屏与重启的来源 —— 两者不是同一件事。

## 9. 「LUT + V-Log 同时」在 RC5 的设计里是被禁止的

`clean-log-prepare` 第一步是 `restore_factory`，它要求 View4 指向原厂。挂了任何
LUT（我们的 `lut.sh apply` 就是把 View4 指到 A18）之后它直接拒绝：

```
ERR View4 is not factory; disable other LUT mods and power-cycle first
```

2026-11-20 15:29 在机上实测到这一行（`/tmp/nx-lut-lite/clean-log-prepare.log`），
当时 A18 挂着 `vk_fuji_soft`。

所以结论只有两条：

| 想做什么 | 怎么做 |
|---|---|
| 用 RC5 的真 V-Log | 先 `lut.sh off` 撤掉 LUT —— **两者不能同时** |
| 要 log 和风格同时生效 | 把 log 烘进 LUT（`tools/lutgen/bake_log.py`），一条表里两件事 |

## 10. 为什么「实用功能」里那两项看起来"不可用"

两个不同的原因，都跟功能本身无关：

1. **V-Log 开关**：被上面那条 `View4` 门控拒绝。而 `gamma-reload.sh` 只回一个原因码
   `CLEAN_LOG_PREPARE_FAILED_RESTORE_FIRST`，真因只写在它自己的日志里 ——
   弹窗上那句原因码对人没有意义，何况……
2. **Metol 版本 / 所有弹窗**：`popup_timeout` 是独立窗口，但会被 nxui 这块
   override-redirect 面板**盖住**。所以 `ver.sh` 三个弹窗在跑、用户看不到 →
   "点了没反应"。

改法（nxui + vlog.sh）：

- 子进程结束时把输出里**最后一行 `ERR`/`FAIL`/`REJECT`** 搬到面板状态行上显示
  （面板自己永远在最上层，不依赖窗口层叠）；
- `vlog.sh` 遇到 `View4 is not factory` 时输出一句中文：
  `ERR 已挂 LUT：先 lut.sh off 再开 V-Log`，同时弹窗也换成这句。

实测：挂 `vk_fuji_soft` 后按 V-Log 开关，面板输出
`ERR 已挂 LUT：先 lut.sh off 再开 V-Log`。
