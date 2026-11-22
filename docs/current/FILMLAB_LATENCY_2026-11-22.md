# FilmLab 发送链路提速（2026-11-22）

上一轮定位到"一次 preview 790ms，其中 preenter 380ms、参数发送 400ms"。本轮把这两段逐一拆开实测，发送段压到 68ms，preenter 原样保留。

## 结论

| 指标 | 改前 | 改后 | 变化 |
|---|---:|---:|---:|
| preview 端到端（真发） | 790 ms | **510 ms** | −35% |
| 其中 preenter（档位确认） | 380 ms | 380 ms | 未动 |
| 其中 pw_direct（七维发送） | 400 ms | **100 ms**（工具本体 68 ms） | −75% |
| 同配方连按（七维相同不发送） | 440 ms | **400 ms** | 只剩档位确认 |

改后总时的 75% 是 preenter 的一次 `getusr 20`。**它不能再省**，理由见 §3。

## 1. 400ms 的真实构成

`pwsend.arm seq` 一次（7 对 + 提交位，`--sleep 20`）：

| 段 | 实测 | 性质 |
|---|---:|---|
| 进程启动（820KB，zig -O0 动态链接） | ~60 ms | 加载 734KB 的 `libcapture-fw-prod.so` 之外的自身成本 |
| `dlopen(libcapture-fw-prod.so)` | 73 ms | 固定；常驻可省，本轮未做 |
| `mcb_init` + `ignore_init_checking` | 2 ms | 幂等，重复调用 0 ms |
| 8 次 `SetVariableDataMCB` | **0.4 ms** | ★ 不是 16ms/次，是 0.05ms/次 |
| 条间 `sleep 20`×7 | 142 ms | 纯等待 |
| 收尾 `usleep(100000)` | 100 ms | 为"读回"留的，preview 不读回 |

后两项 242ms + 二进制加载 60ms ≈ 300ms 是纯粹的浪费。`pwsend.c` 里"16ms/次"的注释与实测差 320 倍，是历史误记。

## 2. 去掉两段等待的依据（不是猜）

`seq` 分支的 sleep 与尾部 usleep 都是 A1 时代留下的保守值。svdm 是同步返回（rc=0 即已入 MCB 队列），队列本身 FIFO。用一个 `-nostdlib` 探针（`pwprobe.arm`，只依赖 `libdl.so.2`）测：

| 条间 sleep | 8 条合计 | 进程墙钟 |
|---:|---:|---:|
| 20 ms | 142.4 ms | 221.2 ms |
| 5 ms | 37.2 ms | 110.0 ms |
| 0 ms | 0.4 ms | 77.6 ms |

**sleep=0 的正确性验证**（这是唯一不能被"看起来没问题"替代的部分）：

1. 基线 `varlist`：`R/G/B=102/100/97 HUE=11 SAT=7 SHARP=8 CON=9`，prefman slot9 同值。
2. `pwfast 112 111 108 12 8 9 10`（七维全改，sleep=0，无尾等待）
   → 读回 `R/G/B=111/111/107 HUE=12 SAT=8 SHARP=9 CON=10`
   七维全部落地，末条提交位也生效（R/G/B 的 ±1 是增益量化，标量四维逐位相等）。
3. **prefman slot9 全程未变**（preview 不写槽，语义保持）。
4. 发回原值 → 读回逐维复现基线。

即"去掉间隔不丢参数"有读回证据，不是靠 rc=0 推断。

## 3. preenter 的 350ms 为什么不能再省

`st cap capdtm getusr 20` 实测 340~430ms（中位 350ms），与"读什么"无关：

| 命令 | 耗时 | 说明 |
|---|---:|---|
| `getusr 99`（无效 id） | 340~360 ms | 纯启动 |
| `capdtm`（无子命令） | 350 ms | 纯启动 |
| `getusr 20` | 350 ms | 有效读 |
| `capdtm varlist` | 370 ms | 注释里"1.31s"的记录是错的 |
| `prefman get <key>` | **10 ms** | 18KB 二进制，但只有槽值 |

`time st` 的结果是关键：**real 0.347s / user 0.300s / sys 0.020s** —— 它在烧 CPU，不是在等 IO。`LD_TRACE_LOADED_OBJECTS` 显示它链 **83 个共享库**（icu、pulse、glib、X11、sqlite、ssl、turbojpeg…）。340ms 是 83 个库的重定位加构造函数，无法用参数绕开。

试过并否定的两条捷径：

- **`SetUserDataMCB`**（与 `SetVariableDataMCB` 同族导出）：`rc=0` 但不改档位。`st` 把档位切到 CUSTOM2 后用它切回，读回仍是 CUSTOM2。它不是 `setusr` 的底层。
- **`st cap iqr`**：同样是 `st`，同样 340ms。

`prefman` 只有 10ms，它存的是槽位值，不是运行时的 `eIQ_ID_EFFECT_MODE`，不能替代档位确认。

**结论**：在不逆 CAP DTM 的 IPC 协议、或不常驻的前提下，350ms 是"每次真读档位"的下限。而每次真读是用户明确要求的（此前的 90s TTL 缓存因标记未绑定档位字段，会让 A2 提交位把七维写进用户当时所在的自定义2/3，已撤回）。

## 4. 产物与接线

**`test_server/pwsend/pwfast.c` → `pwfast.arm`（3312 B）**

- `-nostdlib -O2`，原生 syscall，NEEDED 只有 `libdl.so.2`（避开相机老 glibc 的版本需求）
- 退出用 `exit_group(248)`；用 syscall 1 时 dlopen 的库线程会让进程变僵尸（实测踩过）
- 固定发 8 条（`0x134..0x13A` + `0x13B=1`），无间隔、无尾等待，**恒全量**（0.4ms，不需要"只推变化维"那套复杂度）
- 用法 `pwfast.arm [-v] R G B HUE SAT SHARP CON`；值域 R/G/B 0..200、其余 0..20；越界与缺参均拒绝（rc=1）
- 输出一行 `pwfast: 8/8 ok 68ms (init 68ms)` 或 `pwfast: FAIL at #N id=…`

**`scripts/filmlab.sh` 接线**

```sh
PWFAST=${PWFAST:-/opt/usr/nx-ks/pwfast.arm}
```

`pw_direct` 的真发分支优先走 `pwfast`，**`pwfast` 不存在时逐字回到 `pwsend seq` 老路**（已实测：`PWFAST=/nonexistent` 时 preview 800ms，行为与改前一致）。

同一次部署还带上了 11-22 的 P1 回归修复：相机上原来的 `filmlab.sh` 仍是 90s TTL 缓存版，本地已把 `_preenter_cached` 改为恒 `return 1`（旧实现保留为 `_preenter_cached_legacy`）。

顺带修正 `filmlab.sh` 里四处"getusr20 约 1.31s"的注释 —— 实测 340~430ms，按旧值算出来的"最坏 26s"上限也一并改掉。

## 5. 验证

| 项 | 结果 |
|---|---|
| `preview portra400` ×3 | 510 / 400 / 410 ms（第 2、3 次七维相同，跳发送） |
| `neutral` | 71 ms 发送，`✓ neutral (100/100/100 10/10/10/10)` |
| 回退 `PWFAST=/nonexistent` | 走 `pwsend seq --sleep 20`，800ms，成功 |
| 参数非法 | `300` → `value out of range`；缺参 → usage；均 rc=1 |
| 槽 9 污染 | 全程未变（`102/101/98 11/7/8/9`） |
| 动作后状态 | 档位 `PW_CUSTOM1`，ISP 回到槽 9 值 |

部署位置：`/opt/usr/nx-ks/{filmlab.sh,pwfast.arm}` + `/mnt/mmc/scripts/filmlab.sh`，两处 md5 一致（`filmlab.sh 70cc9e0e`）。旧版备份在相机 `/opt/usr/nx-ks/filmlab.sh.bak-1122-pre-fast`。

## 6. 未做

- **preenter 350ms**：要降它只有两条路——逆 CAP DTM 的 IPC 协议自写客户端，或让一个进程常驻。两条都没试。
- **常驻 pwfast**：还能再省 70ms（dlopen 73ms），但 preview 只剩 510ms 里的 70ms，收益 14%，不值得引入进程生命周期风险。
- **commit 回归**：commit 会写槽 + `prefman save`，本轮没跑（会改用户数据）；只验证了 preview / neutral / 回退三条读写路径。
- **录像共存**：pwfast 与 `0x13B` 提交位在录像中的行为未测。
- **其它调用点**：`pw_direct` 是唯一入口，`neutral`、`apply --fast`、CLI 全路径都经过它；但 NXUI 面板的长按/连按节奏未做人因复测。

## 7. 口径

- 计时用 `/proc/uptime`（10ms 量化）+ 进程内 `CLOCK_MONOTONIC`（µs）；sh 侧 10ms 精度，工具本体 68ms 取三轮中位。
- 510ms 是"已在 CUSTOM1 浏览配方"的常态值。首次进入（当前不在 CUSTOM1）要额外 `setusr` + 回读，会更慢。
- 本轮的 510ms 不含显示、取景、NXUI 面板重绘；面板侧另有自己的刷新开销。
