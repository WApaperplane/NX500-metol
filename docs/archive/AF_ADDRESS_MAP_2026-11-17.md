# AF 地址重定表（2026-11-17）

> 起因：曲线/对焦静态分析中，子代理报出与既有文档冲突的地址（称"前九轮全部少 0x1000"）。
> 本表用**字符串锚点法**重定，不采信任何一方的地址，只看指令。
>
> 方法：扫 chunk `0x20000–0x570000` 全部 32 位字，识别 `movw`+`movt` 同寄存器对，
> 组合出目标地址；落在字符串区（`0x80600000–0x80780000`）且可解为 ASCII 的即为候选引用。
> 共得 **821** 条字符串引用、**73** 条 AF 相关。原始表：`E:/NXWA/tmp_afaddr/af_refs.txt`。
> 函数起点用 `push {...,lr}`（`(w&0x0FFF0000)==0x092D0000 && bit14`）建 17,612 个候选，
> 取"不大于引用点的最大起点"。

## 1. 裁定：文档地址体系基本正确，个别标注错

| 锚点 | 字符串 | 引用点 | 函数起点 | 与旧文档对比 |
|---|---|---|---|---|
| `SetHoldFaceBox` | `SetHoldFaceBox %d` | `0x80140DF8` | **`0x80140DE8`** | ✅ `0x140de8` 完全一致 |
| `USE CONTRAST AF` | 同名字符串 | `0x8034081C` | **`0x803406DC`** | ✅ `0x3406dc` 一致 |
| `PAF Only:ToCAF` | 同名字符串 | `0x8033DB5C` | **`0x8033DA8C`** | ❌ 文档写 `0x34470c` |

⇒ **子代理"全部少 0x1000"的说法不成立**（三缺一，两条对一条错）。
⇒ `PAF Only:ToCAF` 这条日志的函数归属需按本表更正为 `0x8033DA8C`；
   `0x8034470C` 处是另一个正常函数，不引用该字符串。

## 2. 新增函数锚点（此前无记录）

| 地址 | 锚字符串 | 推断功能 | 证据级 |
|---|---|---|---|
| `0x802A07C4` | `PafAdjust.csv` | 相位 AF 调整参数区（栈帧 0x2FC，含 double 常量 100.0/10.0） | L2 |
| `0x802A0DDC` | `PafAdjust_d.csv` + `,` | 该参数区的日志打印（vtable+0x18 虚调用） | L2 |
| `0x802AE128` | `[GROUP] ### SLOW-AF MODE START NOTIFIED` | **慢速 AF 模式启动**：写 `GAF+0x5c = 1`，调 `0x8034BEBC(r1=2)` | L1 |
| `0x802AE19C` | `[GROUP] ### S0-AF MODE START NOTIFIED` | **S0-AF 模式启动**：写 `GAF+0x5c = 2`、`GAF+0xac4 = 3`，调 `0x8034BEBC(r1=1)` | L1 |
| `0x802C3078` | `AfMain` | AF 主入口 | L2 |
| `0x802C3384` | `AfDebug` | AF 调试入口 | L2 |
| `0x802C3518` | `AfSub` | AF 子流程 | L2 |
| `0x802D8ED8` | `AeCalcAFParamContiConti(%x)` | **AE 计算 AF 参数**（连续/连续） | L1 |
| `0x803562E8` | `DECIDE MAIN FOCUS AREA (%d)` | 主对焦区域决策 | L1 |
| `0x802AD2EC` | `updateAfInfo(%08X,%08X, %d)(%d,%d)` | AF 信息更新 | L1 |
| `0x802AD6EC` | `CAF determined by AAF` | CAF 由 AAF 判定 | L1 |
| `0x80145FC8` | `Average Y Data of Main Face(%d)` | 主脸平均亮度（暗光链路候选） | L1 |
| `0x8033992C` | `CAFDataManagement::InitializeBeforeS1` | CAF 数据初始化 | L2 |
| `0x802F1864` | `AF_BTLOG` / `AfLensCallBack_Ready` / `AFLensCallBack_Detach` | 镜头回调组 | L1 |

## 3. ★ 关键修正（2026-11-17 二次复核）：800 ms 覆盖「S0-AF 之外的全部模式」

`0x2F6C48` 的判据是 `cmp r2,#2`，`r2 = GAF+0x5c`：

```
2f6c88  cmp  r2, #2
2f6c8c  movne r8, #0x320     ; ≠2 → 800 ms
2f6c90  moveq r8, r5         ; ==2 → 0
```

**`GAF+0x5c` 不是二值开关，是 0–6 的模式号。** 全镜像只有 8 个写入点（`strb/str [GAF+0x5c]`，
用机器码模式 `(w & 0x0FF00FFF) == 0x05Cx005C` 扫出 787 个候选，再逐个回溯基址得 8 个确属 GAF）：

| 写入点 | 值 | 场景（锚字符串 / 判据） |
|---|---|---|
| `0x802AE250` | **0** | 复位（`cmp r3,#2; bhi` 只有 ≤2 才走到） |
| `0x802AE150` | **1** | `CAF::SlowAFStart` → `[GROUP] ### SLOW-AF MODE START NOTIFIED` |
| `0x802AE1C4` | **2** | `[GROUP] ### S0-AF MODE START NOTIFIED` |
| `0x802AE334` | **3** | 未定名（前置条件 `GAF[2] & 0x80`） |
| `0x802AD124` | **4 / 5 / 6** | 按条件三选一（`movne #5` / `movne #6` / `moveq #4`） |
| `0x802F44C8` | **5** | `AFDeterminedProc`（chk 0x427） |
| `0x802F4720` | **6** | `HighSpeedContiShotNormalRelease`（chk 0x399，高速连拍） |

⇒ 语义应为：

| 模式 | `GAF+0x5c` | FD 去抖 |
|---|---|---|
| S0-AF（单次 AF，半按拍照） | 2 | **0 ms** |
| 其余全部（空闲 0 / SLOW 1 / ? 3 / 4 / AF 判定 5 / **高速连拍 6**） | ≠2 | **800 ms** |

**修正我上一版的说法**：不是"800 ms 只影响 SLOW-AF"。子代理已证 `SlowAFStart` 的自动触发路径
在 v1.12 里不可达（唯一调用点上游是孤儿函数 `0x800ABC8C` 与调试命令 `slafsta`）。
但闸门是 `≠2` 而非 `==1`，所以 **CAF、高速连拍、AF 判定等所有非 S0-AF 场景都吃 800 ms**。

这对补丁意义更大：改 `0x2F6DD4` 影响的是"除半按拍照外的全部场景"，而半按路径完全不受影响。

`0x8034BEBC(r1=2 / r1=1)` 是两模式共同的下一步调用，参数即模式号，可作进一步追踪的锚点。

## 4. 待查

| # | 事项 | 抓手 |
|---|---|---|
| 1 | `PafAdjust.csv` / `_d` 是编译进固件还是运行时从卡上加载 | 看 `0x802A07C4` 内有无 `fopen`/`open` 调用；若只有虚调用日志即为编译期常量 |
| 2 | SLOW-AF 由谁触发（是否与低照度或 CAF 相关） | 扫 `bl 0x802AE128` 的调用点 |
| 3 | `0x8034BEBC(mode)` 内部如何用模式号 | 反汇编该函数 |
| 4 | `AeCalcAFParamContiConti` 是否消费 ISO/BV | 反汇编 `0x802D8ED8`，看入参来源 |
