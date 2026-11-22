# NX500 暗光 AF 慢 —— 软件侧静态溯源（v1.12 / p7_wrapped.elf）

- 日期：2026-11-17
- 对象：`E:/NXWA/fw/v1.12/p7_wrapped.elf`（唯一 PT_LOAD，文件偏移 0x1000 对应镜像基址 0x80000000，镜像长 0xB84280）
- 方法：字符串锚点 → 常量池 `ldr rX,[pc,#imm]` 反查 → 回溯函数序言（`push {…,lr}`）→ 逐函数线性反汇编（每个函数从真实序言起解，避免跨函数线性扫描失步）。
- 边界：纯静态，不连机。凡推不出的一律标"未知"。

---

## 0. 首先修正上一轮结论中的地址约定

镜像里指针字面量的取值满足：

```
vaddr = file_off - 0x1000 + 0x80000000
```

即 `vaddr = file_off + 0x7FFFF000`。验证：字符串 `"PAF Only:ToCAF(Peak NG, Comp=1)"` 位于 **file_off 0x74ccd8**，代码池中存的指针是 **0x8074bcd8**（= 0x74ccd8 - 0x1000 + 0x80000000）。

前九轮给出的地址（0x34470c / 0x3406dc / 0x34a13c / 0x2f0ce4）**比真实 file_off 少 0x1000**，实际位置是：

| 上一轮写法 | 实际 file_off | 实际函数名 |
|---|---|---|
| 0x34470c proPAOToContrastAFCheck | **0x34570c** | 同名，确认 |
| 0x3406dc proPAOAARGetUseContrastAF | **0x3416dc** | 同名，确认 |
| 0x34a13c CAFSequenceNxRoughFineScan::Initialize | **0x34b13c** | 同名，确认 |
| 0x2f0ce4 "相位许可六输入" | 0x2f1ce4 / 0x2efce4 均对不上 | **无法复现**，见第 5 项 |

后文一律使用**真实 file_off**。

---

## 1. 结论摘要

1. **暗光并没有"关掉 PDAF"的显式门控。** 全镜像范围内没有找到"照度 < X → 禁用相位"的分支；相位入口（`checkPhaseAfRunOnCaf` 0x34857c）只看模式位、镜头挂载、AF 锁状态、序列状态，不看 LV/ISO/BV。
2. **暗光影响 AF 的路径是"信号质量 → PAF 判 NG → 回退反差"，不是"策略切换"。** 三条实证链：
   - `DecideAfKneeMode`（0x335cf0）用**画面 ymax/ymin** 判 `LOW_LUMINANCE`，把 KneeMode 写进 IQ id `0x40011`；`SetAfFilterToDsp`（0x3363d4）读回该值去配 DSP AF 滤波器。低照度 → 高 ISO → coring 抬升（0x3358a4 有 ISO>600 的线性抬升公式）→ 反差评价幅度被压。
   - `IIR1 MAX IS TOO SMALL` → 假峰值判定（0x33ee00 起，阈值取 `GAF+0x1bc4`）→ 触发 `RETRY CRAF SEQUENCE`。
   - `proPAOAARGetUseContrastAF`（0x3416dc）在 PAF-only 复核连续失败时，`obj+0x8c` 累加到 **3** 就返回"用反差"，走 `USE CONTRAST AF(#2)`。
3. **暗光下多出来的时间主要花在"重试/重扫"和"等 AE 收敛"，不在单次扫描步长上。** AE 等待上限 **5 帧**（`obj+0x164 <= 4`，0x34e5fc）；PAF→反差前有 **3 次** PAF-only 复核；反差侧粗扫的"无有效数据"会 `RETRY SCAN`，每区域重试阈值 **2 或 3**（取决于 `GAF[0xd]&0x10`，0x36150c）。
4. **`eIQ_ID_AF_ASSIST_LAMP`（id 4）在本固件里只被写、不被 AF 逻辑读。** 全镜像 `mov r1,#4 / movt r1,#4` 只有一处（0x2f2d6c），是 AF 模块初始化时清零。

---

## 2. 逐项排查结果表

| # | 排查方向 | 结论 | 关键地址 / 证据 |
|---|---|---|---|
| 1 | 照度是否参与 AF 门控 | **部分有**：有低照度分支，但作用于"滤波器/膝点"，不作用于"相位许可" | `DecideAfKneeMode` 0x335cf0；`KneeMode(LOW_LUMINANCE)` 串 0x74b9a0；写 IQ 0x40011 @0x335eb0 |
| 1a | "低照度 → 禁用相位" | **无** | 相位入口 0x34857c 全部条件见第 5 节，无 LV/ISO 比较 |
| 1b | "低照度 → 延长扫描" | **无直接证据**；有"低照度下停在粗扫不前进" | `LOW-CON in LOW-LUMINANCE at the NEAR/FAR limit --> STAY AT ROUGH-SCAN` 串 0x74ea8c，引用点 0x347320（函数 0x3470fc） |
| 1c | BV/LUX/GAIN/DARK 字符串与 AF 交叉 | **弱**：`PWRCTL…LV`、`[Live] iso` 等均不在 AF 模块；AF 模块内只有 `LV:%d` 打点 | AF 区字符串 `SEQUENCE STATE(...,LV:%d,...)` 0x74f5c8 @0x34a9f4；`MICRO SCAN CHECK ... LV:0x%02X, ISO:0x%x` 0x74d5d8 @0x3403fc |
| 2 | AF 与 AE 交互 | **有** | AF 状态机 `onStateWaitAeConverged` 0x34e5b4（等 AE 收敛，上限 5 帧）；`onAeConverged` 0x34de84；AF 读 AE 侧双精度量 `GAF+0xb18`（由 `CAeLensIF` 写，0x2d4260），阈值 **5.0** 判断（0x341974） |
| 2a | AF 前抬增益/延曝光 | **未找到**：AF 模块内无对 AE 增益/曝光时间的写操作 | AF 区无 SetParameter 到 AE 增益 id 的写点（对比：`IQ_AE_SetCaptureGain` 在 AE 模块 0x5dc3d8） |
| 3 | CDAF 扫描范围/步长 | **部分**：步长不是常量，由 FPS×速度系数算出 | `Initialize` 0x34b13c：0x34b2e4 取 0x30001（fps）→ `+0x32`、除 100 → r5；再对 0x60000/0x60001/0x60006 三档调 `0x343508`（速度）与 `0x3408a0`（`100*speed/den`） |
| 3a | 判"未找到"并重扫 | **有** | `Fine:RETRY_ROUGHSCAN` 串 0x74ec80；`THERE'S NO ENOUGH VALID DATA ... RETRY SCAN` 0x752428 @0x361544；`TOO FAST PEAK CONFIRM ... RETRY SCAN` 0x7523fc @0x3615d4 |
| 3b | 最大重试次数 | **2 或 3**（按模式位） | 0x36150c：`moveq ip,#3 / movne ip,#2`（依据 `GAF[0xd]&0x10`），0x361530 比较 |
| 4 | `+0x8c` 计数器生命周期 | **已确认** | 递增 0x341708/0x34171c；判 3 0x34175c-0x341768；清零 0x34b2a0（Initialize）、0x33d88c（序列状态初始化） |
| 5 | 0x60084 六输入 | **未复现该函数**；0x60084 是 IQ 状态槽，8 处引用，1 写 7 读 | 写 0x2f3188；读 0x2f5234 / 0x2f8fc0 / 0x34882c / 0x348d1c / 0x349b14 / 0x368cac / 0x36acb8 |
| 6 | AF 辅助灯 id 4 | **只写不读**：仅在 AF 初始化清零，无任何读者，也无"某条件下强制 0" | 0x2f2d6c→0x2f2d7c `SetParameter(0x40004, 0)`；全镜像 `mov r1,#4;movt r1,#4` 仅 1 处 |

---

## 3. 逐项详述

### 3.1 照度是否参与 AF 门控

**能确认的低照度分支（作用在评价信号上）：`DecideAfKneeMode` @ 0x335cf0**

```
335d40  ldr  r6,[pc,#0x1dc]        ; =81550408  全局 AF 上下文 GAF
335d4c  movw r3,#0x2a08
335d54  str  r4,[r6,r3]            ; GAF+0x2a08 = ymax（带 ±0x27 迟滞）
335d58  rsb  sl,r5,r4              ; ydiff = ymax-ymin
335d5c  add  r3,r3,#4
335d60  str  r5,[r6,r3]            ; GAF+0x2a0c = ymin（带 ±9 迟滞）
...
335dc0  cmp  r4,#0x77              ; ymax <= 119 ?
335dc4  cmple r8,#1
335e14  cmp  r5,#0x13              ; ymin <= 19 ?
335e1c  mov  r3,#0x78
335e40  mov  r4,#5                 ; → KneeMode = 5 (LOW_LUMINANCE)
```

判定门限（立即数，精确）：

| KneeMode | 名称（日志串） | 判据 |
|---|---|---|
| 1 | `KneeMode(LOW_CONTRAST)` 0x74b9e0 之前的串 0x74ba1c | ydiff 落在中间档（0x335ef8） |
| 2 | `KneeMode(LOW_CONTRAST_LOW)` 0x74b9e0 | ymax 与 0x258(600) 比较 + `ydiff >= ymax/4`（0x335de0-0x335e0c） |
| 3 | `KneeMode(LOW_Y_LEVEL) (ymax)(zero_count vs 20)` 0x74baf4 | 特殊模式分支（`GAF[0x5c]==2/3` 或 `GAF[3]&0x20`），0x335e48-0x335e68 |
| 4 | `KneeMode(LOW_L_Y_LEVEL)` 0x74ba54 | `ymax < 0x258(600)`，0x335e70-0x335ed4 |
| 5 | `KneeMode(LOW_LUMINANCE)` 0x74b9a0 | `ymax <= 0x77(119)`（即 <120）且 `ymin <= 0x13(19)`（即 <20），0x335dc0-0x335e40 |
| 6 | `KneeMode(HIGH_CONTRAST)` 0x74baa8 | `ymax > 0xf9f(3999)`，0x335e80-0x335e9c |

- 迟滞：ymax **0x27=39**，ymin **9**
- 低亮度中间档：0x320(800) 分界（0x335e78-0x335edc）
- 其余比较值：0x4b0(1200)、0xfa(250)、0x12c(300)、0x14(20，zero_count 阈值)

**该结果的去向**：0x335ea0 `bl 0x33373c`，即 `SetParameter(this, 0x40011, kneeMode)`。读者是 `SetAfFilterToDsp` 0x3363d4（0x336420 `GetParameter(0x40011)`），它把 KneeMode 与 ISO 一起送进 `UpdatetHpfLpfFrequencyCoring`（0x3361a0）→ 0x3358a4 计算 coring：

```
3358a4  cmp r3,#6
3358a8  cmpne r1,#0x258        ; ISO > 600 且 knee != 6
3358c4  sub r3,r5,#0x64        ; (iso-100)
3358c8  add r3,r3,r3,lsl #2    ; *5
3358cc  lsl r3,r3,#4           ; *80
3358d0  add r3,r3,#0xc40       ; +3136
3358d4  movw r7,#0x335d
3358dc  movt r7,#0x299c        ; 0x299c335d ≈ 7e8/1e9 比例因子
3358ec  add r7,r7,#0xbe        ; +190
```
即 coring 随 ISO 近似线性上升（ISO>600 起）。coring 抬升 = 反差评价的弱边缘被吃掉 = 暗光下峰值更难被判出。

**没有找到的**：
- 任何 `if (LV < T) disable PDAF` 形式的分支。相位入口 0x34857c 的判定链（见 3.5）只包含：`GAF[0xe]&0x20`、`GAF+0xaec`（镜头挂载）、`IQ 0x40005`（AF 锁状态）、`GAF[0xd]&0x10`、序列状态 `[obj+0x38] ∈ {0x101,0x102,0x103}`。
- `BV` / `LUX` / `DARK` 与 AF 函数的交叉引用：AF 区（0x2e0000-0x380000）内未出现引用照度字符串的代码。`liveview BV=%f`（0x73f278）在 AWB 模块，与 AF 无关。

### 3.2 AF 与 AE 交互

**有耦合，方向是"AF 等 AE"，不是"AF 抬 AE"。**

1. `AFSequenceRFS::onStateWaitAeConverged` @ 0x34e5b4：
```
34e5c0  ldr  r2,[r3,#0xc0]     ; GAF+0xc0 == 1 才认为可继续
34e5c8  cmp  r2,#1
34e5cc  beq  0x34e5ec
...
34e5f8  ldr  r2,[r0,#0x164]    ; 已等待帧计数
34e5fc  cmp  r2,#4
34e600  bgt  0x34e5d0          ; >4 → 放弃等待（置 obj+0x168 = -1）
34e618  add  r2,r2,#1
34e61c  str  r2,[r4,#0x164]
34e620  mov  r0,#0              ; 返回 0 = 继续等
```
→ **AF 启动前最多等 5 帧 AE 收敛**（0,1,2,3,4）。暗光下 AE 收敛本身慢，这 5 帧是纯等待开销；若 `GAF[0xf]&4` 置位则跳过等待（0x34e604-0x34e614）。

2. `onAeConverged` @ 0x34de84：AE 收敛回调，检查 `[obj+0x160]`，写回 `GAF[0x5c]` 等状态。

3. **AF 读 AE 侧的量**：`doSmartReverseDirection` 0x3418b4 里，`GAF+0xb18` 是一个 64 位双精度量，与常量 **5.0** 比较：
```
341950  movw r3,#0xb18
341954  ldrd r2,r3,[r3,r6]      ; 取 GAF+0xb18 的 double
341974  movt r3,#0x4014         ; 0x40140000 = 5.0
341978  bl   0x4fb568           ; double 比较
34197c  cmp  r0,#0
341984  beq  0x3418d4           ; 不满足 → 不反向
```
`GAF+0xb18` 的唯一写入者是 `CAeLensIF` 构造/更新路径（0x2d4260，函数 0x2d4128，字符串 `CAeLensIF-Constructor`）。**语义未定**（候选：AE 目标亮度/LV 的浮点表示，或镜头侧 defocus 量）。

4. AF 不写 AE：AF 区没有对 AE 增益/曝光 id 的 `SetParameter`。所以"AF 前提高增益/延长曝光"这条**不成立**。

### 3.3 CDAF 扫描策略（`RoughFineScan`）

**扫描步长不是常量，是按 FPS 与速度系数算出来的**（0x34b13c）：

```
34b2e4  bl    0x333948          ; GetParameter(this->[8], 0x30001) → fps*100 之类的整数
34b2e8  movw  r3,#0x851f
34b2ec  movt  r3,#0x51eb         ; /100 的魔乘
34b2f0  add   r0,r0,#0x32        ; +50（四舍五入）
34b2fc  rsb   r5,r0,r5,asr #5    ; r5 = (fps_val+50)/100
34b300  mov   r1,#0x60000
34b308  bl    0x343508           ; 速度计算（含 SPEED BOOST / 16-50PZ 限速）
34b30c  mov   r2,r5
34b318  bl    0x3408a0           ; step_time = 100*speed/den
34b324  movt  r1,#6              ; 0x60001（第二档，细扫）
34b344  movt  r1,#6  (0x60006)   ; 第三档（微扫）
```

`0x3408a0` 本体：
```
3408a0  cmp  r2,#0
3408ac  moveq r4,#1
3408b0  cmp  r1,#0xff            ; 0xff = "用 FPS 而不是固定速度"
3408b8  mov  r0,#0x64
3408bc  mul  r1,r0,r1            ; speed*100
3408c0  sdiv r0,r1,r4            ; /den
3408c8  (0xff 分支) 取 0x60071 → *100/den
```
→ 最终"每步时间/步长"由 **FPS × 三档速度系数** 决定，**没有固定的粗扫/细扫脉冲常量**。扫描范围同样由镜头行程与 `SCAN MAX` 运行时计算（0x358a14 读 `GAF+0x1f6c`，不足时报 `[ERROR] SCAN MAX is NOT VALID`）。

**判"未找到"并重扫（有）：**
- 0x361544：`THERE'S NO ENOUGH VALID DATA ... RETRY SCAN`
- 0x3615d4：`TOO FAST PEAK CONFIRM(%d) ... RETRY SCAN`
- 0x74ec80 `Fine:RETRY_ROUGHSCAN`（`processFineScan` 0x347ddc 的状态返回）
- 0x3605a4 `PEAK NOT FOUND --> REMAIN CONTINUE`

**重试上限（0x36150c，精确）：**
```
3614f8  ldrb ip,[r3,#0xd]
361500  and  ip,ip,#0x10
36150c  moveq ip,#3              ; 该模式位为 0 → 上限 3
361510  movne ip,#2              ; 为 1 → 上限 2
361530  cmp  r1,ip
361534  bge  0x3613c8            ; 达到上限 → 收工（不再重扫）
```

**"未找到"之后的行为**：`proPAOToContrastAFCheck`（0x34570c，日志 `PAF Only:ToCAF(Peak NG, Comp=0)`，0x74ccd8）把序列状态置 `0x101` 并进入反差序列；调用点在 0x34816c（函数 0x348048）。

### 3.4 `+0x8c` 计数器的完整生命周期

**递增**（`proPAOAARGetUseContrastAF` @ 0x3416dc，唯一调用者 0x345400，位于 `processPhaseAfOnlyAfAfterRecheck` 0x345114）：
```
341708  ldr  r3,[r5,#0x8c]
341710  add  r3,r3,#1
34171c  str  r3,[r5,#0x8c]       ; 每次进入就 +1，无前置条件
```

**判 3**：
```
341754  cmp  r7,#0                ; r7 = 第7个参数（useDir/allowWait）
341758  beq  0x3417b8             ; r7==0 → USE CONTRAST AF(#4)，直接转反差
34175c  ldr  r2,[r5,#0x8c]
341764  cmp  r2,#2
341768  bgt  0x3417a8             ; count>2（即第3次）→ USE CONTRAST AF(#2)，返回 1
34176c  ldr  r1,[pc]              ; =8074c998 "PAF DIR : WAIT PEAK ... (%d)"
341770  bl   0x34dbd0
```
→ 第 1、2 次：打 `WAIT PEAK`，返回 0（继续等 PAF 峰值）；**第 3 次：返回 1（用反差）**。

**清零**：
- 0x34b2a0 `str r5,[r4,#0x8c]`（r5=0），在 `CAFSequenceNxRoughFineScan::Initialize` 0x34b13c 中，与 `+0x80/+0x88/+0xcc/+0xd4` 一批状态一起清 → **每次 AF 序列初始化清零**。
- 0x33d88c `str r1,[r4,#0x8c]`，在序列状态初始化（DriveFocus 侧，0x33d7ec 函数内）→ **状态重入时也会重置**。

**结论**：`3` = **同一条 AF 序列内，PAF-only 复核失败的第 3 次调用**，不是"3 帧"也不是固定时长。每次调用之间至少隔一次 AF 帧节拍 + 一次镜头驱动（`processPhaseAfOnlyAfAfterRecheck` 内还含 `0x344d54`、`0x33f19c` 的复核/平均逻辑），所以实际耗时 = 3 ×（帧周期 + 驱动 + 复核），暗光下驱动更慢、帧周期更长，因此非线性放大。**不是永久转反差**：序列 Initialize 或状态重入即复位。

### 3.5 0x60084 的"六输入"

**无法复现上一轮描述的 0x2f0ce4 六输入函数。** 依据：
- 0x2f0ce4 落在函数 0x2f0b78 中部（该函数操作 `0x4aa/0x4ac/0x4b4` 偏移与直方图/加权求和，不是相位门控）。
- 全镜像没有 `movw/movt` 组合 `0x1001e`（`hub_get(...,0x1001e)` 不存在）。
- 全镜像 `[x,#0xecc]` 只有 6 处（0xdce14 无关、0x13168c、0x31f74c/0x31f75c、0x31f7e4），集中在 **0x31f730 / 0x31f7a0** 这个小模块（镜头状态/挂载标记），与 AF 相位门控不是同一处。

**能确认的是：0x60084 是一个 IQ 状态槽（AF 序列/锁定状态），8 处引用，1 写 7 读。**

| 类型 | 地址 | 所在函数 | 用途 |
|---|---|---|---|
| 写 | 0x2f3188 | 0x2f3118 | `SetParameter(0x60084, r5)`，r5 来自 0x2f1ce4 的返回值 |
| 读 | 0x2f5234 | 0x2f5178 | 非 0 → 触发一次虚调用（`[r3+0xc]`） |
| 读 | 0x2f8fc0 | 0x2f8db0 | 非 0 → 跳过；为 0 才继续读 0x40000 |
| 读 | 0x34882c | 0x34857c `checkPhaseAfRunOnCaf` | 非 0 → 走 0x34884c，配合 `obj+0x10d/0x10c` |
| 读 | 0x348d1c | 0x348bbc `beforeExecuteMain` | 非 0 → 分支到 0x348dcc |
| 读 | 0x349b14 | 0x349ad0 | 非 0 → `r7 = (GAF+0x1f30 <= 1)` |
| 读 | 0x368cac | 0x368c74 | 非 0 → 序列状态比较（0x110/0x10e） |
| 读 | 0x36acb8 | 0x36abac | 非 0 → 与 `obj+0x1b0` 比较，决定 2/其它分支 |

其中 0x349b24 与上一轮"`GAF+0x1f30 < 2` 限制相位入口"完全对上：
```
349b24  movw r3,#0x1f30
349b28  ldr  r7,[r5,r3]
349b2c  cmp  r7,#1
349b30  movgt r7,#0        ; >1 → 0
349b34  movle r7,#1        ; <=1 → 1
```
→ **真实约束是 `GAF+0x1f30 <= 1`（即 <2），且仅在 0x60084 != 0 时生效。**

补全后的相位许可（可实证的部分，0x34857c）：
1. `GAF[0xe] & 0x20` == 0（模式允许）
2. `GAF+0xaec` != 0（镜头挂载）
3. `IQ 0x40005` == 0（AF 未锁定）
4. `GAF[0xd] & 0x10`（走哪条子路径）
5. `[obj+0x38] ∈ {0x101,0x102,0x103}`（序列状态）
6. `IQ 0x60084` == 0（且 `GAF+0x1f30 < 2`）
第 7、8 个输入（原"其余两个未完全定名"）在本函数里体现为 `GAF[1]&4` 与 `GAF[4]&4` 两个分支，语义分别是"AAF/连续 AF 标志"与"某捕获态标志"，**未最终定名**。

### 3.6 AF 辅助灯（`eIQ_ID_AF_ASSIST_LAMP`, id 4）

- 字符串 `eIQ_ID_AF_ASSIST_LAMP` 在 0x701e24（IQ 名称表）。
- 全镜像 `mov r1,#4 / movt r1,#4`（= id 0x40004）**只有一处**：0x2f2d6c，上下文是 AF 模块初始化的批量清零：
```
2f2d2c  mov r1,#1 ; movt r1,#4 ; mov r2,#0 ; bl 0x33373c   ; 0x40001 = 0
2f2d44  mov r1,#2 ...                                        ; 0x40002 = 0
2f2d58  mov r1,#3 ...                                        ; 0x40003 = 0
2f2d6c  mov r1,#4 ...                                        ; 0x40004 = 0  ← 辅助灯
2f2d80  mov r1,#5 ...                                        ; 0x40005 = 0
```
- **没有任何读者**（AF 区与全镜像都没有对该 id 的 `GetParameter`），因此也**不存在"某条件下被强制 0"的逻辑**——它只是初始化时被清一次。
- 辅助灯的实际开关不在 AF 模块（应在闪光/Strobe 侧，`CStrobeOperation` 0x706b58 附近），与"AF 慢"无因果。

---

## 4. 关键函数反汇编摘录

### 4.1 `proPAOAARGetUseContrastAF` 0x3416dc —— 3 次后转反差的完整判定

```
3416dc  push {r4,r5,r6,r7,r8,lr}
3416e0  mov  r5,r0                 ; this
3416e8  mov  r4,r2                 ; area
3416ec  mov  r8,r1                 ; paf_pos
3416fc  mov  r6,r3                 ; usePeak
341700  ldrb r7,[sp,#0x28]         ; 第7参数
341708  ldr  r3,[r5,#0x8c]
341710  add  r3,r3,#1
34171c  str  r3,[r5,#0x8c]         ; ★ 无条件 +1
341734  ldr  r3,[pc,#0xd8]         ; =81550408
341738  add  r2,r4,r4,lsl #1       ; area*3
34173c  add  r3,r3,r2,lsl #3       ; GAF + area*24
341740  ldr  r3,[r3,#0xe4]         ; 每区域状态
341744  cmp  r3,#0
341748  blt  0x3417e0
34174c  cmp  r6,#0                 ; usePeak
341750  bne  0x341778
341754  cmp  r7,#0
341758  beq  0x3417b8              ; → "USE CONTRAST AF(#4)"
34175c  ldr  r2,[r5,#0x8c]
341764  cmp  r2,#2
341768  bgt  0x3417a8              ; → "USE CONTRAST AF(#2)" 返回 1
34176c  ldr  r1,[pc,#0xa4]         ; "PAF DIR : WAIT PEAK ... (%d)"
341770  bl   0x34dbd0
341774  b    0x34178c              ; 返回 r6=0（继续等）
341778  mov  r0,r5
34177c  bl   0x33ee00              ; PAF 假峰值检查
341780  subs r6,r0,#0
341784  beq  0x3417cc              ; 假峰值 → 0x340fd8 recheck peak
```

### 4.2 `CAFSequenceNxRoughFineScan::Initialize` 0x34b13c —— 计数器复位点

```
34b1c4  ldr  r1,[pc,#0x3bc]        ; 日志 tag "CAFSequenceNxRoughFineScan..."
34b1cc  … 遍历区域对象，虚调用 [r3,#8] …
34b200  ldr  r0,[r4,#0x50]
34b208  bl   0x372d48
34b20c  mov  r7,#1
34b220  str  r5,[r4,#0xd4]         ; r5 = 0
34b224  str  r5,[r4,#0xcc]
34b228  str  r5,[r4,#0xd8]
34b22c  str  r7,[r4,#0xc8]         ; =1
34b230  bl   0x33373c             ; SetParameter(0x6004a, 0)
34b234  …                         ; 0x6004b / 0x6004c / 0x6004f = 0
34b298  str  r5,[r4,#0x80]
34b29c  str  r5,[r4,#0x88]
34b2a0  str  r5,[r4,#0x8c]         ; ★ 回退计数器清零
34b2a4  strb r5,[r4,#0x10d]
34b2b0  str  r3,[r4,#0x13c]        ; GAF+0x1f34
34b2c4  movw r3,#0x1be4
34b2cc  str  r5,[r6,r3]            ; GAF+0x1be4 = 0
```

### 4.3 `DecideAfKneeMode` 0x335cf0 —— 低照度 → KneeMode

（核心片段已见 3.1；末尾统一走 `SetParameter(0x40011, kneeMode)` @0x335ea0-0x335eb0）

### 4.4 低照度 PAF 方向分支 0x3462f4 → 0x3463d4

```
3462f4  push {r4,r5,r6,lr}
346310  mov  r0,r4
346314  bl   0x3418b4             ; doSmartReverseDirection
346318  subs r6,r0,#0
34631c  bne  0x3463d4             ; ★ 非 0 → 低照度分支
...
3463d4  mov  r0,r4
3463d8  bl   0x3419e4             ; 计算 PAF 方向目标位
3463dc  ldr  r1,[pc,#0x6c]        ; =8074d8fc "LOW-LUMINANCE PAF DIRECTION DRIVE TO (%d)"
3463ec  bl   0x34dbd0
```
`doSmartReverseDirection` 的前置条件（0x3418b4-0x341910）：
`GAF[0]&1 == 0`、`GAF[0xd]&8 != 0`、`GAF+0xc58 <= 3`、`mode == 2`。
其中 `GAF+0xc58` 在上限 3 内才允许"智能反向"，超过就放弃 → 暗光下 PAF 抖动会消耗这 3 次机会。

### 4.5 AE 收敛等待上限 0x34e5fc

```
34e5f0  cmp  r2,#0                ; obj+0x160（AE 收敛标志）
34e5f4  beq  0x34e5d0             ; 未就绪 → 直接放弃
34e5f8  ldr  r2,[r0,#0x164]
34e5fc  cmp  r2,#4
34e600  bgt  0x34e5d0             ; >4 → 放弃等待
34e618  add  r2,r2,#1
34e61c  str  r2,[r4,#0x164]
34e620  mov  r0,#0                ; 返回"继续等"
```

---

## 5. 未解项

1. **`GAF+0xb18` 的双精度量语义未定。** 唯一写入者 `CAeLensIF`（0x2d4260），AF 侧以 5.0 为阈值比较。可能是 AE 的 LV/BV 浮点值，也可能是镜头 defocus 量。需动态验证（在 0x341954 断点读该 double）。
2. **`eIQ_ID_AF_ASSIST_LAMP`(4) 在本固件中确认"只写不读"，但辅助灯由哪个模块驱动未定位**（疑似 `CStrobeOperation` 0x706b58 一带，未展开）。
3. **粗扫/细扫的绝对步数与行程范围没有固定常量**，由 FPS 与 `0x343508` 的速度系数动态算；`0x343508` 内部的完整分支（SPEED BOOST / PANNING / 16-50PZ 限速）只读了前 0x40 字节，未完全展开。
4. **`0x60084` 的写入值语义未定**（0x2f3118 写入的是 0x2f1ce4 的返回值），只知"非 0 时抑制相位并受 `GAF+0x1f30<2` 约束"。
5. **"六输入"函数的原始地址 0x2f0ce4 无法复现**，可能来自更早的镜像版本或人工归纳；本报告给出的是从代码实际反出的相位入口条件（0x34857c）及其 8 个引用点。
6. **`0x36150c` 的 2/3 阈值语义**：从上下文看是"每区域有效数据/峰值确认的重试上限"，但 `[r7 + (idx+0x786)*4]` 是表查询，未确认该表是常量表还是运行时缓冲。

---

## 6. 一句话总结

暗光 AF 慢不是"暗光把相位关了"，而是**暗光把反差评价质量打下去（KneeMode=LOW_LUMINANCE → coring 抬升 → IIR1 幅度小 → 假峰值/无有效数据）**，导致 PAF-only 复核连续失败，走到 `obj+0x8c == 3` 才切反差；切换前还要等最多 5 帧 AE 收敛，切换后粗扫又要按 2/3 次上限重扫。时间花在**重试与等待**，不在单次扫描步长。
