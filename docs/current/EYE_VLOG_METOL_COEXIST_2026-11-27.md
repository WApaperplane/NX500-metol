# Eye AF × Log117 × metol 共存：迁移方案与开发交接

日期 2026-11-27 · 交给 Terra 实现 · 当前机身状态：r2 守卫在位，Eye 仅 prepare，Log117 仅文件层，未重建过快照

> ⚠ 2026-11-27 晚补正：**§2 与 §8 的「自造 `modstack.sh` 事务器」已被推翻。**
> 上游 0.4.0 R1 的 `stage-postflash.py` + `coexist_rebuild.py` 就是同一台机、同一条流程的完整安装器，
> 且它**不重写 `di-camera-app.service` 的 `ExecStart`**（走 `hib_post.service → resume unit → wake.sh`），
> 与我们 `launch.sh` 路线冲突。动手前先读 `REBUILD_PATH_DECISION_2026-11-27.md`。

---

## 0 结论

1. **这不是"安装器框架"问题，是"快照代次"问题。** Eye 库必须在 p9 里有映射；任何后置钩子都来不及（蓝牙钩子 boot+37.8s，App exec boot+12.2s，差 25.6s）。所以唯一路径是：**在一次 bootmode 1|3 的开机里完成注入，然后让那次开机被写成 p9**。
2. **p9 只能整块写、不能增量、不能重复。** 因此不存在"先装 Eye 再装 V-Log"的两步走。两个 mod 必须在**同一次重建**里就位。框架的真实职责是**事务守卫 + 代次记账**，不是编排两套安装脚本。
3. **Log117 的接入点是文件层，不涉及冻结。** 我们与上游用的是同一个 RC5 引擎（`5edf35e0…`），Log117 只是换一个 arm + target 132→117。它必须先装完再重建，否则快照冻结的是旧代码。

---

## 1 事实基线

### 1.1 已实测（可直接当设计前提）

| 事实 | 证据 |
|---|---|
| 正常开机 = 内核冷启 + 用户态恢复，`systemd 不重读 unit` | `docs/archive/TASKFLOW_2026-10-16.md:143`，P5 真机失效链 |
| `/sys/power/bootmode` = 2 为正常开机；4 为 bootshell cold-boot | `docs/current/HANDOVER_2026-10-16.md:91` |
| 守卫与 `launch.sh` 都只接受 bootmode **1\|3**；2 直接 `NOT_A_CAPTURE_BOOT` | `verify-before-capture.sh:98`、`launch.sh:mode` |
| 改 unit / `daemon-reload` 对恢复路径无效（App 不从 ExecStart 起） | 2026-10-16 第七轮判据 5-7 全灭 |
| 蓝牙钩子链 `init.sh` = boot+37.8s；App exec = boot+12.2s | `scripts/dicam/dicam_patch_install.sh:13` |
| 我们的 gamma 引擎 = `5edf35e0ba83…`，与上游 `rc5_gamma_arm_sha256` 一致 | `scripts/gamma-install.sh:42`、上游 `gray-calibration.json` |
| 上游 Log117 = 另一个 arm；输入 2965 时 RC5→132，Log117→117 | 同上 `log117_target/replay` |
| 我们的 Log117 安装器已存在，装完 `ACTIVATED=false`，不重建快照 | `scripts/gamma-log117-install.sh` |

### 1.2 推断（必须先验，否则整条链是猜的）

| # | 待验 | 验法 | 影响 |
|---|---|---|---|
| V1 | bootmode 1 与 3 的差别；哪一个才是原厂 rescue 重建那次 | 真机取 `/sys/power/bootmode` + `hibernation.sh` 调用链打印 | 决定 `launch.sh` 的 eligible 白名单是否要收窄 |
| V2 | 重建那次开机是否真的经过 ExecStart（即 launch.sh 会被调用） | 在 launch.sh 里落一行带 bootmode 的日志，重建后回读 | 若不经过，整条路径不成立 |
| V3 | 恢复后 App 的 `/proc/<pid>/maps` 是否含 `nxl-eye-af-yunet.so` 且 inode 与文件一致 | `bootmode=2` 开机后 `maps` + `stat` | 这是"冻结成功"的唯一硬判据 |
| V4 | 恢复后 bluetoothd 是否仍是带钩子的那份（钩子在内存还是文件） | 比对 `bluetoothd` 哈希 + 钩子日志 mtime | 决定 metol 是否需要额外 wake 动作 |

---

## 1.5 为什么绕不过快照重建（四条路逐一否决）

「集成进 metol 框架而不刷机」的诉求，实质是问：**能不能在 App 已经跑起来之后让它带库。**
四条路都堵死，且各有实测或原理级证据：

| 路径 | 做法 | 否决理由 | 证据 |
|---|---|---|---|
| 后置钩子 | 蓝牙链 boot+37.8s 注入 | App exec = boot+12.2s，符号早已解析完，差 25.6s | `dicam_patch_install.sh` 实测时序 |
| 重启 App | 改 unit + `daemon-reload` + `restart di-camera-app` | **`restart` 该 unit 即卡死整机，须拔电池** | 10-13 实测、10-15 复述、T2 安装器因此"只就位不重启" |
| 运行期注入 | ptrace + `dlopen` | 库靠 LD_PRELOAD 覆盖同名符号（`exports.map` 只导出 `_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii`）+ `dlsym(RTLD_NEXT,…)` 取原函数；`dlopen` 时符号搜索顺序与 `RTLD_NEXT` 语义都不成立 | `src/exports.map`、`src/eye-af.c:474` |
| 改 App 二进制 | `DT_NEEDED` 或 patch 启动函数 | 恢复态下 App **不 exec**（从快照恢复），改了不生效 | 10-16 P5 实证（systemd 不重读 unit） |

同时 `#Restart=always` 在原厂 unit 里是注释状态 ⇒ App 若崩不自愈，只能拔电池。
这条让"注入失败"的代价从"重开 App"变成"物理重启"。

**结论**：能集成的是**部署与状态管理**（metol 做文件准备、重建后验证、菜单入口），
不能集成的是**注入时机**——库进内存这一步只能由「bootmode 1|3 那次冷启动」完成。

### 术语：重建快照 ≠ 刷机

| | 刷机 | 重建快照 |
|---|---|---|
| 写什么 | SLP 包 / rootfs 分区 | 只写 p9（80–150 MiB 内存镜像） |
| 影响 `/opt/usr` | **会清空**（2026-11-27 实测：gamma 树与 eye 残留全没了） | 不动 |
| 最坏结果 | 系统起不来，只能重新刷 | p9 失效退回冷启动；有守卫时拒绝即交回原厂 |
| 可回滚 | 难 | 备份 p9 即可整块写回 |

所以"不想刷机"和"不想重建快照"是两个不同强度的约束。前者我们同样反对（今天刚被清过一次），
后者才是这次要做的事。

### 如果坚持零 p9 写入

只剩两条，代价都比重建大：

1. **放弃 Fast Start**：让快照机制失效，每次开机走完整冷启动 ⇒ `ExecStart` 执行 ⇒ LD_PRELOAD 生效。
   代价是丢掉原厂秒开（App 完整初始化 12s+），且需先验证删 `/etc/snapshot` 后系统仍正常。
2. **重写为 inline hook + 自建注入器**：ptrace 注入后在 `FaceDetection` 开头打跳转。
   这会改掉 EYE28 的"算法原版"前提，要自己写 ARM trampoline，且注入失败 = 拔电池。

---



## 2 抽象纠正：要的是"快照代次事务"，不是"安装器框架"

| 维度 | 安装器框架 | 快照代次事务（实际需要） |
|---|---|---|
| 可重复性 | 幂等，可反复跑 | **不可重复**，一次写入定生死 |
| 失败语义 | 回滚文件即可 | 回滚需要**整块 p9 备份**，或救援路径 |
| 顺序 | 无强序 | 强序：文件层 → 冷启注入 → 写 p9 → 恢复验证 |
| 归属 | 每个 mod 各自一份 | **单一 owner**，不允许第二个写入者 |

所以要做的东西只有一件：**一个只允许一次 in-flight 的 rebuild 事务器**，它同时管住 Eye 注入、Log117 引擎版本、metol 钩子状态、以及 p9 写入。

---

## 3 架构：三层 + 一个代次

```
L2 运行时层（恢复后动作，可重复）
   gamma 曲线切换 / LUT 槽 A18 / 菜单按键 / V-Log toggle
   └ 只依赖文件与 p7 写入，不进 p9

L1 文件层（/opt/usr、/usr/local/lib、/usr/bin）
   Eye payload · launch.sh · gamma 引擎(Log117) · metol 全套 · 守卫
   └ 幂等安装，可在任意时刻改；但改完必须重走 L0 才生效于恢复路径

L0 冻结层（p9 = 内存快照）
   App 进程 + Eye 库映射 + bluetoothd/钩子 + 引擎进程
   └ 只能通过「bootmode 1|3 开机 → hibernation.sh 写盘」产生
```

**单一 owner 原则**：`hib_post.service` 只能有一个 resume 服务；`/dev/mmcblk0p9` 只能有一个写入者；`/opt/usr/nx-ks/gamma` 只能有一个安装器在跑。

---

## 4 必须先解的四个冲突

| # | 冲突 | 现状 | 解法 |
|---|---|---|---|
| C1 | r4 守卫要求 App 是"原厂 stdout 形态"，但注入路径下 ExecStart 是 `launch.sh` | `verify-before-capture.sh` 的 `writer_reason` 会判 `WRITER_IS_NOT_CURRENT_STOCK_MAIN` | 升 r5：接受两种 App 形态；新增 `launch.sh` 哈希 + `nxl-eye-af-yunet.so` 哈希进 pin 集 |
| C2 | `wake.sh` 要求 `armed`/`capture-origin`/`launch-claim` 三件套且 boot_id 匹配，语义是"一次性 arm" | 恢复路径下每代都要验，不能靠一次性 armed | 代次语义改写：`generation/<N>/expected.json` 记录该代应有的库 inode/哈希，恢复后**逐项比对实际内存** |
| C3 | `gamma-log117-install.sh` 会 `mv` 整个 gamma 树；已冻结的引擎进程仍持旧 inode | 换引擎后不重启进程 = 内存里还是旧代码 | 强制顺序：Log117 必须在 rebuild **之前**装完；rebuild 后禁止再换引擎（检测到换树即要求重走 L0） |
| C4 | 上游 Unified R1 的入口绑定它自己的 `full-menu.sh` + `nxl-keys` | 直接叠装会和我们 nxui 抢按键 | **不叠装**。只借它的两个设计点：单入口、拒绝后仍交回原厂；入口本体用我们自己的 `nx-eye-resume.service` |

---

## 5 状态机与阶段契约

状态落在 `/opt/usr/nx-modstack/state/generation`（单行，代次号）+ `state/phase`（单行）。

```
ABSENT
  │ files.stage（幂等：metol 完整 + eye payload + log117 + r5 守卫）
  ▼
FILES_STAGED
  │ preflight（只读：固件/写入器/载荷/USB=0/p9 未 busy/电量/卡）
  ▼
PREFLIGHT_OK
  │ arm（写 generation N + 期望清单 + boot_id_A）
  ▼
ARMED
  │ 重启，bootmode ∈ {1,3}
  ▼
CAPTURE_BOOT ──(launch.sh eligible → LD_PRELOAD → claim)──► 注入完成
  │ 守卫 --at-preparation / --at-image-write 两段放行
  ▼
IMAGE_WRITTEN（p9 = 新代次）
  │ 再开机，bootmode=2
  ▼
RESUME_VERIFIED（maps/inode/引擎哈希 三项匹配）
  ▼
ACTIVE
```

失败分支一律**不进下一状态**，且 `IMAGE_WRITTEN` 之前失败必须保持 p9 原样（这正是 r4 两段门禁的职责）。

### 5.1 每个阶段的契约

| 阶段 | 输入 | 输出（stdout 单行 RESULT=） | 硬判据 |
|---|---|---|---|
| `files.stage` | 组件包目录 | `FILES_STAGED` / `SOURCE_VERIFY_FAILED` | 每个 payload 的 `payload.sha256` 通过 |
| `preflight` | — | `PREFLIGHT_OK` / 具体拒绝码 | USB=0、`/proc/swaps` 无 p9、`/` 可写切换正常 |
| `arm` | generation N | `ARMED gen=N` | 期望清单写完并 `sync`；不允许已有 in-flight |
| `capture` | — | `IMAGE_WRITTEN gen=N` | 守卫两段都 PASS；`hibernation.sh` 退出 0 |
| `verify` | — | `RESUME_VERIFIED gen=N` | 见 §7 |
| `rollback` | 备份 p9 路径 | `ROLLED_BACK gen=M` | 仅允许在 `IMAGE_WRITTEN` 前自动执行 |

### 5.2 期望清单格式

`/opt/usr/nx-modstack/state/generation/N/expected.json`

```json
{
  "generation": 3,
  "built_at_boot_id": "dbb7e6ce-...",
  "components": {
    "eye_lib":    {"path": "/usr/local/lib/nx-eye-af/nxl-eye-af-yunet.so", "sha256": "...", "must_be_mapped": true},
    "camera_app": {"path": "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app", "sha256": "..."},
    "launcher":   {"path": "/usr/local/lib/nx-eye-af/launch.sh", "sha256": "..."},
    "gamma_arm":  {"path": "/opt/usr/nx-ks/gamma/bin/nxl-gamma-reload.arm", "sha256": "...", "profile": "log117"},
    "bt_hook":    {"path": "/usr/sbin/bluetoothd", "sha256": "..."}
  },
  "p9_sha256": null
}
```

`p9_sha256` 由**电脑端**填（机身无 128MiB 以上哈希能力时用分块累加），不在机身上算整块。

---

## 6 文件布局

```
/opt/usr/nx-modstack/
  state/
    generation                  # 单行代次号
    phase                       # 单行状态
    generation/N/expected.json
    generation/N/receipt/       # 各阶段回执，只追加
  p9-backup/
    mmcblk0p9-before-genN.img   # 只在允许 rebuid 前由显式操作产生
```

**不放在 `/opt/usr/nx-eye-af`**：那个目录归 Eye 预加载，语义是"库与门禁"，不要混进事务状态。

---

## 7 验收判据（恢复路径）

`bootmode=2` 开机后，以下全部成立才算 `RESUME_VERIFIED`：

| # | 判据 | 命令 |
|---|---|---|
| A1 | App PID 可读，exe 是原厂路径 | `readlink /proc/<pid>/exe` |
| A2 | 库已映射且 inode 与文件一致 | `awk '$6==lib' /proc/<pid>/maps` + `stat -c %d:%i` |
| A3 | 库文件哈希 = `expected.json` 里的值 | `busybox sha256sum` |
| A4 | gamma 引擎 arm 哈希 = Log117 的值 | 同上 |
| A5 | 蓝牙钩子仍是带钩那版 | 同上 |
| A6 | 同一次开机内：眼框出现（A-AF + 人脸 Normal） | 人眼计数增长 |

**A2 是唯一不能被"文件对了"替代的判据** —— 文件对而内存里没映射，说明快照没冻住，等于没装。这正是上游 `resume-after-stock.sh` 用 `mapped_dev/mapped_ino` 做的事，我们照抄这个思路即可。

---

## 8 工作分解（给 Terra）

| # | 任务 | 产出 | 依赖 |
|---|---|---|---|
| T1 | 验 V1/V2/V3/V4 | `rea-evidence/20261127-modstack/` 四份 txt | 真机可达 |
| T2 | 守卫升 r5：接受 launch.sh 形态 | `build_eye_capture.py` + 回放测试 | T1.V2 |
| T3 | 事务器骨架 `modstack.sh`：`stage/preflight/arm/capture/verify/rollback` | 1 个脚本 + 状态机测试 | T2 |
| T4 | Log117 接入：`gamma-log117-install.sh` 增加 `--require-profile log117` 供 T3 校验 | 改 1 个脚本 + 哈希链同步 | — |
| T5 | 恢复验证器 `verify-generation.sh`：A1-A5 | 1 个脚本 | T3 |
| T6 | PC 回放：假根跑 6 条失败路径（缺库/库哈希变/引擎变/钩子丢/bootmode 错/armed 冲突） | `test_server/modstack/` | T3-T5 |
| T7 | 上机脚本 + 教程（一步一判据） | `docs/current/MODSTACK_INSTALL_GUIDE.md` | T1-T6 |

**T1 不通过就不要写 T2 以后的代码。** 现在整条链的关键未知只有一个：重建那次开机是否经过 ExecStart。这一点是 2026-10-16 的 P5 事故反过来证明的——unit 改动对恢复路径无效，但**对 bootmode 1|3 是否有效，从未直接验过**。

> ⚠ 2026-11-27 晚：**T2/T3 暂停。** 上游 0.4.0 R1 已经用 `hib_post` 路线绕开了「制作那次是否 exec」这个未知量——它的统一入口在**恢复之后**介入，不需要 `ExecStart` 改写，因此也不依赖 V2。
> 先把 `REBUILD_PATH_DECISION_2026-11-27.md` §5 的路线裁决做完；裁决结果可能是 T2/T3 整段作废。

---

## 9 红线

| 红线 | 原因 |
|---|---|
| 不要在 `IMAGE_WRITTEN` 之后改 `/opt/usr/nx-ks/gamma` | 冻结的进程持旧 inode，改文件不生效，只会让下一次验收误判 |
| 不要引入第二个 `hib_post.wants` 服务 | 抢同一个 resume 时机，顺序不可控 |
| 不要用上游的 `nxl-full040-resume.service` 叠在我们的 nxui 上 | 两套按键监听会互相抢 EV/AEL |
| 不要把"文件装好"当成"已生效" | 恢复路径不读 unit、不读配置，只认内存 |
| 不要在没有 p9 备份的情况下第一次重建 | p9 不可增量回滚 |

---

## 附：与上游 0.4.0 的关系

| 上游做法 | 我们是否采用 |
|---|---|
| 单入口 `nxl-full040-resume.service` | 结构采用，**本体不采用**（改用自己的 `nx-eye-resume.service`） |
| 拒绝后仍交回原厂（`touch /tmp/.hib_ready; exit 0`） | 已采用（r4 的两段门禁） |
| `mapped_dev/mapped_ino` 校验库真的在内存 | 采用（A2） |
| 快照重建走原厂 rescue | 采用 |
| 它自己的菜单/按键监听 | **不采用**（我们用 nxui） |
| `CLEAR` 只撤服务、不改镜像 | 采用同语义：`rollback` 只回文件层，镜像另算 |
