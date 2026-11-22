# T2：交付修订与预置协议

> **2026-11-20 文档合并：** 本文由 2 份同主题记录合并而成，原文按时间顺序保留、未删改，标题层级下调一级。原始文件已归档到 `docs/archive/`，文件名见各节「来源」。

1. T2 投递链修订（2026-10-15）
2. T2 —— 统一预设与适配器协议 v1（2026-10-16）

---

## 1. T2 投递链修订（2026-10-15）

> 来源：`T2_DELIVERY_REVISION_2026-10-15.md`（已归档至 `docs/archive/`）

> **目的**：把 10-13 实机发现合并入 T2 投递件；给出「生效路径」再设计的候选与论证材料（供裁决）。
> **依据**：`CAMERA_ONLINE_PROBE_2026-10-13.md`（§1-2 时序与卡死实测 / 附 C 休眠联锁）；`NATIVE_UI_ROADMAP_2026-10-15.md` §5 修订块；`MEM_INJECTION_EVAL_2026-10-15.md` §3。
> **落地**：`scripts/dicam/dicam_patch_install.sh` 与 `dicam_patch_rollback.sh` 已修订（本日）；`dicam_patch_status.sh` 未改（只读工具，保持原样）。
> **口径**：✅ = 已证（实测）｜🔶 = 推断/设计 ｜ ❓ = 未决（附验证法）

---

### 0. 结论

0. ★★ **裁决（2026-10-15，用户拍板）：生效路径 = 候选 a（P5 前置）**。实施件已制作（`t2_premount.sh` + `dicam_p5_install.sh` + `dicam_p5_rollback.sh` + status 集成），详案见 **§6**。同批拍板：首探样本暂不上机（不着急）。
1. **两个缺口已确认**（§1）：T2 的两条设计前提被 10-13 实测推翻——mount 晚于 App exec 约 25s（时序）；「立即生效」所用的 restart 会卡死整机（风险）。
2. **脚本已修订**（§2）：install/rollback 去 restart、语义改为「只就位 → 待自然重启」、注释与判据对齐实机事实；语法/行尾/无残留 restart 均已复核 ✅。
3. **「生效路径」再设计**：三候选（§3，保留备查）——**已裁决为 a（P5 前置）**，落地见 §6。
4. 在实机验证前，**T2 不得宣称「安装即生效」**——脚本输出已按此措辞（「已就位；生效需等 App 自然重启 / P5 后为『下一次正常开机』」）。

---

### 1. 两个缺口（证据）

#### 1.1 缺口 A：mount 晚于 App exec（时序）

- 实机实测：di-camera-app exec = **boot+12.2s**；init.sh（bluetoothd 钩子链）= **boot+37.8s**（CAMERA_ONLINE_PROBE §1-2，`/proc/<pid>/stat` 第 22 字段）。
- bind-mount 语义：改变「路径 → inode」的解析；**已 exec 的进程持有 exec 时打开的文件映射，不受后续 mount 影响**。
- ⇒ 每次冷启动，init.sh 建立的 mount 都晚于本次 App exec；旧前提「本块必须早于 `di-camera-app.service`」被推翻。
- 未决（= 候选 c 要回答）：本机为 hibernation 快照式启动（附 C）。「日常关机开机」究竟是 re-exec 还是 snapshot-resume；mount 表跨 resume 是否保留——均未实测 ❓。

#### 1.2 缺口 B：restart 卡死（风险）

- 实测：`systemctl restart di-camera-app` ⇒ **整机卡死**（须物理重启），机理推断为 hibernation 联锁（附 C）。
- 旧脚本第 4 步「立即生效」= mount + restart ⇒ **已删除**。
- 牵连修订：`TASKFLOW_2026-10-12` 中 W-3/E-D4 演练判据（「restart 后画面正常 ⇒ 机制可用」）全部作废，改「自然关机开机」路径。

---

### 2. 脚本变更清单（本日）

| 文件 | 变更 |
|---|---|
| `dicam_patch_install.sh` | ① 头部新增「10-15 修订」段（时序实测 + restart 风险 + 「只就位」语义）；② init.sh 追加块注释更正（删旧前提，注明实际生效点见本文档）；③ 第 4 步「立即生效（重启 App）」→「建立 bind-mount（不再重启 App）」+ 新增 4b「生效语义（必读）」段；④ 收尾输出明确「当前运行进程仍是旧件」「功能判据需在自然重启后核对」 |
| `dicam_patch_rollback.sh` | ① 头部修订说明；② init.sh 清理 sed 兜底行补充（匹配新版注释行）；③ 第 3 步去掉 restart → 「正常关机→开机恢复」+ 明确当前进程仍是补丁件 |
| `dicam_patch_status.sh` | 未改（只读检查工具） |

复核 ✅：三者 `bash -n` 通过；无 CRLF（铁律 99）；全仓库 grep 无残留 `systemctl restart` 调用（仅注释中的禁止说明）。

---

### 3. 三候选：生效路径再设计（★ 已裁决 = a，2026-10-15；本节保留备查）

#### 候选 a：unit 本体前置挂载（P5 式）★ 已选（实施见 §6）

- **a1（ExecStartPre）**：给 rootfs `/usr/lib/systemd/system/di-camera-app.service` 的 [Service] 增一行
  `ExecStartPre=-/opt/usr/nx-ks/dicam/t2_premount.sh`
  （`-` 前缀：失败不阻止启动；脚本内做存在性判断 + `mount --bind`）。
- **a2（ExecStart wrapper）**：改 `ExecStart=` 为 `/opt/usr/nx-ks/dicam/t2_launch.sh`，脚本内先条件 mount 再 `exec` 原二进制。**时序保证最硬**（mount 与 exec 零间隙）。
- 前提：rootfs remount rw 一次（与既有 `bluetoothd` wrapper 同类先例——mod 早已在 rootfs 写文件，「零 rootfs 写入」本就不成立）。
- 代价：unit 为 rootfs 物，**刷机后被官方重写** ⇒ 需扩展「刷机后自动重装链」（把「检查/重打 unit 行」加入 *install.sh 全量安装分支*——注意 D-5 纪律「日常增量链不碰 rootfs」，本项应为全量分支专属）。
- 风险：低（`-` 前缀保证不阻止启动；脚本失败 = 退回无挂载 = 原厂行为）。
- 判据：正常关机开机后，`mount | grep di-camera-app` 在 App 启动前已就位；`st app film` 可用。
- 恢复：remount rw + 删行/wrapper 还原 + 自然重启。

#### 候选 b：T1 就地补丁（放弃 T2 抗刷机性，换确定性）

- **操作**：remount rw → 按 `c2_bytepatch.v2.json` 就地改写两个补丁点（P1 8B + P2 248B）→ 无需 mount、无需 init.sh 块。
- 优点：**彻底无时序问题**（exec 时加载的就是改后文件）；文件身份可即时验证（md5 == `48d94bd6…`）。
- 代价：rootfs 被就地写入（96% 满——同尺寸改写不增空间）；**刷机后补丁随 rootfs 被官方重写而消失** ⇒ 接受「刷机后重放」（手动或专门链，不塞日常增量链）。
- 风险：低（写错 = App 崩；有 `c2_bytepatch.v2.json` 逆写 + 官方件备份）；恢复：反向字节写回。
- 判据：目标文件 md5 == `48d94bd63e4479fc931ae1a1fee4e94b`（v2 补丁）。

#### 候选 c：生命周期实测（先做，可能原样解锁 T2）

- **问题**：是否存在「晚于 mount（boot+37.8s）的 App exec 时刻」？
  - 若「日常关机开机 = 快照 resume，进程不 re-exec」→ 需另找 re-exec 触发点（或转 a/b）；
  - 若「resume 保留 mount 表 **且** 存在某时点 re-exec」→ **T2 原样可用**（init.sh 维持 mount，re-exec 时自然加载补丁件）。
- **只读采集**（相机在线时）：
  1. 两次「正常关机→开机」后的 `/proc/<pid>/stat` starttime 与 PID 对比；
  2. `/proc/<pid>/maps` 中可执行文件行的 inode/device 变化；
  3. `mount | grep di-camera-app` 跨关机保持性；
  4. journal 的 App 启动记录（判断是否每次 boot 都 re-exec）。
- 代价：低（只读）；不确定性：结论可能「不存在」（则转 a/b）。
- 判据：找到「mount 在位 + App 随后 re-exec」的时点序列。

**建议考察顺序**：**c → a → b**（先拿便宜结论；a 时序最硬、改动可包装；b 最简单但损抗刷机性）。**a 与 b 不互斥**：可先 b 解锁功能验证、后 a 收敛为交付形态。

---

### 4. 上机验证清单（相机在线时执行）

- [ ] 现状快照：mount 表 + App PID/starttime + maps 可执行行（基线）
- [ ] 「正常关机→开机」×2：记录 PID、starttime、maps inode、mount 保持性（= 候选 c 的数据）
- [ ] 若选 a：remount rw → 注入 ExecStartPre/wrapper → 关机开机 → journal 验证 mount 早于 App exec
- [ ] 若选 b：remount rw → 就地补丁 → 验证 md5 → 关机开机 → `st app film` 判据

### 5. 遗留问题（❓）

1. **mount namespace**：App 是否在独立 namespace（若是，init.sh 的 mount 可见性需另验）。
2. **hibernation 恢复路径**对 mount 表与进程状态的影响（候选 c 核心）。
3. 「自然关机」是否总能触发 re-exec——本机快照机制细节未逆清。
4. `TASKFLOW_2026-10-12` 内 W-3/E-D4 原文判据的正式修订（文档层面，待执行）。

---

### 6. ★ P5 实施方案（裁决后落地件，2026-10-15）

#### 6.1 组件清单（源在仓库 `scripts/dicam/`；语法/LF 已复核）

| 件 | 相机端落点 | 作用 |
|---|---|---|
| `t2_premount.sh` | `/opt/usr/nx-ks/dicam/t2_premount.sh` | 被 ExecStartPre 调用；**幂等**条件挂载（补丁体不存在 ⇒ 静默退出=禁用；已挂载 ⇒ 跳过）；任何失败皆 exit 0（不阻止 App 启动） |
| `dicam_p5_install.sh` | `/opt/usr/nx-ks/dicam/` | **注入器**：remount rw → 备份 unit（存 p14）→ 逐行改写注入 `ExecStartPre` → 自检（命中数==1）→ `sync` → remount ro → daemon-reload |
| `dicam_p5_rollback.sh` | 同上 | **恢复器**：精准移除该行（自检 0 命中），与注入器互逆 |
| `dicam_patch_status.sh` | 同上 | 已加「P5 前置」检查段（unit 行在位性） |

#### 6.2 unit 改动（唯一 rootfs 写入点）

```ini
[Service]
+ ExecStartPre=-/opt/usr/nx-ks/dicam/t2_premount.sh    ← 新增（P5）
  ExecStart=/usr/apps/com.samsung.di-camera-app/bin/di-camera-app
```

- `-` 前缀：脚本失败**不阻止** App 启动（硬护栏）。
- 备份：`/opt/usr/nx-ks/dicam/di-camera-app.service.p5.bak`（p14，刷机不被写）。
- 注入方式：纯 POSIX sh 逐行改写（busybox 全兼容；不用 awk/sed 的 `i` 语法）。

#### 6.3 生效语义（为什么覆盖缺口 A）

- systemd 在**每次 unit 启动**时执行 ExecStartPre——严格先于 ExecStart/exec ⇒ **冷启动下 mount 时序硬保证**（不再有 init.sh 的 25s 缺口）。
- **与 init.sh 复挂块并存安全**：两边都幂等（先挂者生效，后者跳过）；init.sh 链退为双保险。
- 🔶 诚实边界（未实测项）：本机日常「关机开机」若为 **hibernation resume**（进程不重新 exec），则 mount 的保持性依赖「快照中 mount 表被保留」——即**至少发生过一次冷启动完成挂载**后，后续 resume 沿用。刷机后重启、断电重启等均为冷启动路径，可完成首次挂载。
- ❗ 与缺口 B 的关系：本方案**全程无 restart**；回退唯「正常关机→开机」。

#### 6.4 刷机后重装链

- 刷机 → rootfs 被官方重写 ⇒ unit 行被冲掉（与 bluetoothd hook 同理）；
- 恢复：SD 全量安装链装回 hook/init.sh 后，**重跑一次 `dicam_p5_install.sh`**；
- ⚠ 暂不改 `install.sh` 本体（安全分发器，改动需单独评估）；「init.sh 自动检查/补 unit 行」的自愈增强列 §6.6。

#### 6.5 上机流程（就绪，待放行）

1. 同步 `scripts/dicam/*` → 相机 `/opt/usr/nx-ks/dicam/`（FTP 或 SD）；
2. `sh dicam_patch_status.sh` 确认补丁体在位；
3. `sh dicam_p5_install.sh`（注入；**无 restart**）；
4. **正常关机 → 开机**；
5. 验证：status 脚本（mount 在位 + P5 行在位）+ `/tmp/dicam_p5_mount.log` + `st app film` 功能判据；
6. 回退：`dicam_p5_rollback.sh` + 正常开关机。

#### 6.6 遗留/增强候选

- init.sh 自愈（自动检查/补 unit 行）——涉及开机自动 remount rw，慎重评估后再做；
- 双挂栈深核对（上机时 `mount | grep` 确认始终单层）；
- daemon-reload 后若本次运行中发生「App 被拉起」，ExecStartPre 亦生效（预期无害，首次上机时观察）。

---
*生成：2026-10-15 · 分支工作树（Codex 克隆）· **裁决已落**（P5 前置 a）；实施件 v1 已就绪（离线编写，未上机验证）。零写机（脚本/文档为本地编辑）。*

## 2. T2 —— 统一预设与适配器协议 v1（2026-10-16）

> 来源：`T2_PRESET_PROTOCOL_2026-10-16.md`（已归档至 `docs/archive/`）

> **任务来源**：`FILMLAB_LUT_NATIVE_UI_GOALS_2026-10-16.md` §5（T2，P0：定义统一预设与适配器协议）。
> **性质**：协议设计 + **可执行模拟后端**（离线）。实机接入属 T3/T5/T8。
> **对应实现**：`test_server/lutpipe/preset_sim.py`（模拟器）+ `test_server/lutpipe/presets/schema_v1.json`（格式样张）。

---

### 0. 设计约束（来自 GOALS，逐条落实）

| GOALS 要求 | 本文对应 |
|---|---|
| 预设以**稳定 ID** 关联 显示名 / PW 参数 / LUT 资产 / 能力说明 | §1 `Preset` 结构（`id` 永不随显示名变化） |
| LUT 资产记录 **来源 / 内容哈希 / 表格式 / 输入色彩条件** | §1.2 `lut` 段（`sha256` + `input_color.verified`） |
| 三原厂自定义槽可作**快捷映射**，但配方库规模不与其绑定 | §1.3 `slot_map`（可选、独立、可空） |
| 状态至少区分：已选择 / 应用中 / 仅预览生效 / 拍摄效果已验证 / 失败 / **已被原厂操作修改** | §2 状态机（+`partial` 部分成功） |
| PW 成功而 LUT 失败 ⇒ **显示部分成功 + 恢复**；不得包装为原子事务 | §2.3 `partial` 语义 + §3.2 `ApplyResult` |
| UI 线程不执行耗时加载 / 同步 shell；完成后回填状态 | §4 异步约定（工作线程 + 有界队列） |
| 第一版：焦点只改选择；确认才应用；滚轮停留预览**后置** | §2.1 状态机首版边界 |
| **「关闭 LUT」= 停止守护 + 恢复确认过的原厂选表/配置**（不得以 identity 代替） | §3.3 `rollback` 语义 + `restore_factory()` |
| PW 与 LUT **应用顺序需实机确定**（不假定 PW 提交不触发 p7 重配置） | §3.4 `order` 可配置 + 顺序未知标记 |
| identity 仅作诊断，不宣称「原厂中性」 | §1.2 `purpose` 字段（`style` / `diagnostic`） |

---

### 1. 预设格式（`nxwa.preset/1`）

#### 1.1 顶层

```json
{
  "schema": "nxwa.preset/1",
  "id": "portra400",                      // 稳定 ID：永不变；文件名/索引均用它
  "display_name": "Portra 400",           // 显示名：可本地化、可改，不承载语义
  "capability": { },                      // §1.2
  "pw": { "R":106,"G":100,"B":93,"HUE":11,"SAT":9,"SHARP":9,"CON":8 },
  "lut": { },                             // §1.2（可缺省）
  "slot_map": { },                        // §1.3（可缺省）
  "meta": { "author":"…","created":"…","notes":"…" }
}
```

- `pw` 字段名 = `recipes.json` 既有键（R_COLOR/G_COLOR/…/CONTRAST）的短名；值域 = A2 直推原始域（R/G/B 0–200；其余 0–20）。
- **`id` 与 `display_name` 强分离**：M2 文案工艺只动显示名，**不得**参与任何寻址（GOALS §3）。

#### 1.2 能力块

| 字段 | 取值 | 语义 / UI 义务 |
|---|---|---|
| `pw` | bool | 含 PW 参数 |
| `lut` | bool | 含 LUT 资产 |
| `preview` | bool | 预览可用（LUT1 直灌或 PW 即时） |
| `still` | `native` / `postproc` / `none` | 成片**机制可达性**：原生直灌可达 / 需后处理 / 不承诺。（E4 后 LUT 直灌 ⇒ 只能 `postproc`/`none`；纯 PW 参数链 ⇒ `native`） |
| `still_verified` | bool | 成片路径**是否已实机验收**。`still=native` 但此值为 `false` ⇒ 状态机**不得**进入 `still_verified`，UI 按「仅预览生效」呈现（诚实边界） |
| `requires_firmware` | `null` / `"A2"` | 固件前提；探测不到 ⇒ PW 段降级「不生效」并如实显示 |
| `lut_asset` | `{ "file","sha256","format","input_color" }` | 见下 |
| `lut_asset.input_color.verified` | bool | **false ⇒ UI 不得宣称色彩准确/即导即用**（GOALS §6） |
| `lut_asset.purpose` | `style` / `diagnostic` | identity 类表必须标 `diagnostic`，且**不可**作为「关闭/恢复」手段 |

#### 1.3 槽映射（可选）

```json
"slot_map": { "slot": 1, "verified": false }
```
- 语义 =「该预设当前映射到某原厂自定义槽」；`verified=false` 表示**只是文案/快捷映射，槽内容一致性未验证**（GOALS §3：改名样机不构成一致性完成）。

---

### 2. 状态机

#### 2.1 状态与转移（首版）

```
select(preset)                confirm()                全部成功
  [idle] ─────────────► [selected] ────────────► [applying] ──────► [applied]
                                                      │                 │
                                    部分成功 ─────────┤                 ├─► [preview_only]      （仅预览生效；成片走 postproc/none）
                                                      ▼                 ├─► [still_verified]    （成片经实机验收后）
                                                  [partial]             └─► [external_modified] （原厂菜单改过参数）
                                                      │
                                       retry_failed() │ rollback()
                                                      ▼
                                             [applied] / [rolled_back]
```

| 状态 | 含义 | UI 呈现义务 |
|---|---|---|
| `idle` | 未选择 | — |
| `selected` | 焦点选中但**未应用**（滚动焦点只改这里） | 不得显示「已应用」 |
| `applying` | 应用中（工作线程） | 忙碌态；**拒绝并发确认**（合并/拒绝重复） |
| `applied` | 全部适配器成功 | 需带 `preview_only` / `still_verified` 子状态 |
| `partial` | 部分适配器失败 | **如实显示哪一部分成功/失败** + 提供「重试失败项 / 回滚成功项」 |
| `preview_only` | LUT/PW 仅预览生效 | 明示「成片不跟随」（当前 LUT 常态） |
| `still_verified` | 成片路径已实机验收 | 置位条件 = `capability.still=="native"` **且** `capability.still_verified==true`（两者缺一即按「仅预览生效」呈现） |
| `external_modified` | 检测到原厂侧改参（PW 值/槽被改） | 显示「已被修改」，禁止继续宣称精确匹配 |
| `rolled_back` | 已恢复应用前状态 | 明示恢复范围（PW / LUT / 两者） |

- **取消语义**：`cancel()` 仅在 `applying` 且适配器支持中断时有效；**已提交的写入不回滚**（除非显式 `rollback()`）——GOALS §3：不能假定取消等于回滚。
- **重复确认**：`applying` 期间的新 `confirm()` 合并或拒绝（标记 `busy`），不得排队执行第二个事务。

#### 2.2 外部改参检测

- **PW**：与 `applied` 时记录的七维快照比对（`varlist`/直推回读）；
- **LUT**：比对 `Cfg.SelLUT` 与守护状态、目标 `LUT0` 地址；
- 任一不一致 ⇒ `external_modified`（**不自动恢复**，由用户决定重应用或清除）。

### 3. 适配器接口

#### 3.1 接口（伪代码，两实现：`pw` / `lut`）

```
Applier:
  name() -> "pw" | "lut"
  probe(preset, ctx) -> ProbeReport        # 固件/落点/守护/表文件/哈希 逐项可用性
  apply(preset, ctx) -> ApplyResult        # 幂等；内部可轮询 cancel 标志
  rollback(preset, ctx) -> ApplyResult     # 回到本适配器 apply 前状态
  classify(preset, ctx) -> ResultClass     # 供控制器估算可行域（可选）
```

#### 3.2 结果分级（**禁用原子化包装**）

```
ApplyResult :=
  ok      : bool
  level   : "full" | "partial" | "failed"
  detail  : { per_step: [...], evidence_ref: "<raw8/…>" }
  restore : RestoreHandle | null            # 供 rollback
```
- **控制器只做逐适配器汇总**：`(pw_result, lut_result)` → 状态机 §2.1。
- **禁止**：把二者合成一个「全成功/全失败」布尔对外。

#### 3.3 `rollback` 与「关闭/恢复」语义（GOALS §3 落实）

| 动作 | 实现 | **禁止** |
|---|---|---|
| `rollback_lut()` | 停止守护 → `op_init(sel=0,…)` 恢复原厂选表 → `lutload restore` | 写 identity 表冒充「关闭」 |
| `rollback_pw()` | 恢复 apply 前七维快照（经 A2 直推）或提示用户在原厂菜单重设 | 假定「参数提交不触发 p7 重配置」 |
| `restore_factory()` | 两者的组合恢复（目标 = 已确认的原厂状态） | 以 identity 作为「原厂中性」的证明 |

#### 3.4 应用顺序（**未定，可配置**）

```json
"order": ["pw", "lut"],          // 首版默认；T4/实机裁决后定稿
"order_tbd": true                // 标记：顺序尚未实机验证（GOALS 要求不得假定）
```
- 顺序不确定的理由：PW 提交（A2 直推 → 槽变更 → 应用链）可能触发 p7 表重载 ⇒ 影响 LUT 存活；反之 LUT 写表需要「抢回态」静默期。二者交互须实机测（转 T4/T7）。

### 4. 线程与生命周期约定

- UI 线程只做：状态转移请求、界面刷新。**不执行**文件 IO / shell / 表装载。
- 工作线程：有界单队列（一次只跑一个预设事务）；完成后以消息回主循环。
- 对象销毁 / 模式切换（拍照↔回放）⇒ 拒绝陈旧请求回写 UI（丢弃带 generation 号 ≠ 当前的完成消息）。
- 失败/取消时**不假设** UI 与硬件一致 ⇒ 重读实际状态（`probe()`）后再显示。

---

### 5. 模拟后端与验证（`preset_sim.py`）

模拟器实现：`PwMock` / `LutMock`（可注入故障）+ `Controller`（状态机）；输出**状态轨迹**。

| 场景 | 注入 | 期望轨迹 |
|---|---|---|
| S1 全成功 | — | `idle→selected→applying→applied→preview_only` |
| S2 部分失败 | lut.apply 失败 | `…→applying→partial`（detail 标明 pw=ok / lut=fail）；可 `retry_failed` |
| S3 取消 | apply 中置 cancel | `applying→`（未提交步不回滚）`→partial/rolled_back` 按显式选择 |
| S4 外部改参 | 应用后 `external_change()` | `applied→external_modified` |
| S5 固件缺失 | `requires_firmware=A2` 且探测缺失 | pw 段降级「不生效」，状态如实标注；LUT 部分照常 |
| S6 关闭语义 | `close_lut()` | 走 `rollback_lut`（**不写 identity**）；轨迹记录恢复动作 |

运行：
```bash
python test_server/lutpipe/preset_sim.py --all        # 全部场景 + 轨迹
python test_server/lutpipe/preset_sim.py --scene S2   # 单场景
```

### 6. 未决（转下游任务）

| # | 项 | 归属 |
|---|---|---|
| 1 | `order`（PW/LUT 顺序）实机裁决 | T4/T7 + 上机 |
| 2 | `still: native` 何时可置位 | T8（成片链验收） |
| 3 | 原厂改参的**可靠检测**（varlist 采样点 / Cfg 读回频率） | T5/T8 |
| 4 | 槽映射一致性（三槽 = 三预设？） | U6 槽语义实验 + T3 |
| 5 | `.mo` 动态显示名（跟随预设） | 后续（M2 已知边界） |

---
*生成：2026-10-16 · 离线（协议 + 模拟后端）；实机接入属 T3/T5/T8。*
