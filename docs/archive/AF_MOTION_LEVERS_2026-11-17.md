# AF 运动主体连续对焦（CAF）跟不上的软件侧杠杆
**静态逆向报告 · NX500 v1.12 · 2026-11-17**

镜像：`fw/v1.12/p7_wrapped.elf`（唯一 PT_LOAD：vaddr `0x80000000`，file off `0x1000`，filesz `0xB84280`）

地址约定（与既有资料一致，已逐条验证）：
- 文中所有地址均为 **镜像内相对地址 rel**；`vaddr = 0x80000000 + rel`，`file_off = rel + 0x1000`
- 例：`0x2f6c48` → vaddr `0x802f6c48` → 文件偏移 `0x2f7c48`
- 反汇编：capstone 5.0.7，`CS_ARCH_ARM / CS_MODE_ARM`；函数边界均向前回溯到 `push {...,lr}` 确认，未跨函数；常量池单独标注

---

## 0. 三项结论表

| # | 议题 | 结论 | 确切地址 | 可改否 |
|---|------|------|----------|--------|
| 1 | 预测阶数硬编码 | **算法号不是"字段"，是 `ExecuteExposurePredictContiH` 在调用预测分派器时硬编码的第 2 个实参**：`mov r1,#1` → `bl 0x36f634`。0 走二阶路径，1 走一阶路径 | 写入：`0x33bff0`（`E3A01001`）<br>分派器：`0x36f634`<br>算法0：`0x36f13c`<br>算法1：`0x36f4f0` | **可以**。三帧历史 AF 结果已在共享环形缓冲里，无需新增状态，改 1 条指令即可 |
| 2 | 800ms 双用常量 | 获得方向（`0x2f6cfc`）与丢失方向（`0x2f6dd4`）共用 `r8`。丢失路径的 **r6 完全空闲**，且 `r5` 与 `r4` 是同一个 GAF 指针（冗余装载） | 获得：`0x2f6cfc`<br>丢失：`0x2f6dd4`<br>常量：`0x2f6c8c` | **可以**。最小改法只需 **1 条指令**（`cmp r8,r2` → `cmp r8,r2,lsl #3`，800→100ms），且 `stage==2 → 0` 语义自动保持 |
| 3 | 主脸打分 | `score = 6×距离名次 + 3×面积名次`，取最大。距离是**未归一化的欧氏平方（像素²）**；名次由冒泡排序（降序）+ 反序编号得到；**没有任何帧间平滑、身份保持或历史偏置** | 打分：`0x143740`<br>排名：`0x14360c`<br>结果写入：`0x1438c4` → `ctx[0x398]` | 可加惩罚项，但需 **2 条指令 + 1 个 4 字节代码洞**；**不需要新增全局变量**（复用 `ctx[0x398]` 的旧值） |

### 对既有资料的两处修正

1. **GAF 基址 = `0x81550408`**，不是 `0x816d429c`。`0x81550408` 来自常量池 `0x2f6e4c`（`ldr r4,[pc,#0x1d0]` @ `0x2f6c74`），`[r4,#0x5c]` 即"内部执行阶段"。`0x816d429c` 是 **trace/log 上下文**，作为 `0x34cbd0 / 0x34ca08 / 0x34ccf4 / 0x34cc78` 的第 1 个参数出现（见 `0x33bfdc`、`0x2f6dd8`、`0x2f6e34`）。凡把 `0x816d429c` 当 GAF 的旧结论，字段偏移都要复核。
2. **`0x349ca8` 本身不是"分派器"**，它是 AF 事件状态机里事件 `0x306` 的一个 case（`mov r0,r5; bl 0x8033cae0; ldr r1,[r5,#0x38]; b 0x80349a78`），`0x33cae0` 只是一个 DriveFocus 步骤 + 日志。真正的"算法 0 / 算法 1"分派在 `0x36f634`。

---

## 一、预测阶数硬编码点

### 1.1 定位路径

```
字符串表 "ExecuteExposurePredictContiH" @ rel 0x624fbc
  → 常量池 0x33c040（函数 0x33bfcc 的 profiler 名字参数）
    → 函数 0x33bfcc = ExecuteExposurePredictContiH
      → 0x33bff0  mov r1, #1
      → 0x33bff8  bl 0x36f634        ← 算法分派器
```

`0x33bfcc` 的调用点只有两处，都在 AF 序列的 drive 步里：`0x2f4840`、`0x2f4908`
（`ldr r0,[r4,#0x98]` = 预测器对象；`ldr r1,[r5,#0xdc]` = 目标时刻；`r2 = r7`）。

### 1.2 上下文反汇编（±12 条）

```
0033bfcc  f0 40 2d e9  push  {r4, r5, r6, lr}
0033bfd0  00 40 a0 e1  mov   r4, r0                  ; r4 = AF 对象
0033bfd4  01 50 a0 e1  mov   r5, r1                  ; r5 = 目标时刻(ms)
0033bfd8  02 60 a0 e1  mov   r6, r2                  ; r6 = 附加参数
0033bfdc  58 00 9f e5  ldr   r0, [pc, #0x58]         ; 0x816d429c  trace ctx
0033bfe0  58 10 9f e5  ldr   r1, [pc, #0x58]         ; "ExecuteExposurePredictContiH"
0033bfe4  01 20 a0 e3  mov   r2, #1
0033bfe8  23 43 00 eb  bl    0x8034ccf4              ; trace_begin()
0033bfec  10 00 94 e5  ldr   r0, [r4, #0x10]         ; r0 = 预测器子对象 (*)
0033bff0  01 10 a0 e3  mov   r1, #1                  ; ★★★ 算法号 = 1（硬编码）★★★
0033bff4  06 20 a0 e1  mov   r2, r6
0033bff8  8d cd 00 eb  bl    0x8036f634              ; ★★★ 预测分派器(r0=predictor, r1=算法, r2=时刻)
0033bffc  05 10 a0 e1  mov   r1, r5
0033c000  00 20 a0 e1  mov   r2, r0                  ; r2 = 预测焦点位置
0033c004  04 00 a0 e1  mov   r0, r4
0033c008  c6 ff ff eb  bl    0x8033bf28              ; 应用到镜头/3A
0033c00c  00 40 50 e2  subs  r4, r0, #0
0033c010  28 10 9f e5  ldr   r1, [pc, #0x28]
0033c014  20 00 9f e5  ldr   r0, [pc, #0x20]
0033c018  03 00 00 0a  beq   0x8033c02c
0033c01c  01 20 a0 e3  mov   r2, #1
0033c020  14 43 00 eb  bl    0x8034cc78              ; trace_end(ok)
0033c024  01 00 a0 e3  mov   r0, #1
0033c028  70 80 bd e8  pop   {r4, r5, r6, pc}
0033c02c  04 20 a0 e1  mov   r2, r4
0033c030  10 43 00 eb  bl    0x8034cc78              ; trace_end(fail)
0033c034  04 00 a0 e1  mov   r0, r4
0033c038  70 80 bd e8  pop   {r4, r5, r6, pc}
```

（*）`[r4,#0x10]` —— 这里 `r4` 是 `0x33bfcc` 的入参对象（调用方传的是 `af_obj[0x98]`），`+0x10` 存放预测器实例指针；预测器实例的构造见 §1.5。

**补丁点：`rel 0x33bff0`（file_off `0x33cff0`），字节 `01 10 a0 e3`（`E3A01001`）→ `00 10 a0 e3`（`E3A01000`）。**

### 1.3 这个值后续被谁读

```
0036f634  00 00 51 e3  cmp  r1, #0
0036f638  10 40 2d e9  push {r4, lr}          ; ← 函数真正起点（cmp 在 prologue 前，合法）
0036f63c  00 40 a0 e1  mov  r4, r0
0036f640  03 00 00 1a  bne  0x8036f654
0036f644  02 10 a0 e1  mov  r1, r2
0036f648  bb fe ff eb  bl   0x8036f13c        ; ★ 算法 0 路径
0036f64c  30 00 94 e5  ldr  r0, [r4, #0x30]    ; 返回最终预测位置
0036f650  10 80 bd e8  pop  {r4, pc}
0036f654  01 00 51 e3  cmp  r1, #1
0036f658  fb ff ff 1a  bne  0x8036f64c        ; 非法算法 → 直接返回旧值
0036f65c  02 10 a0 e1  mov  r1, r2
0036f660  a2 ff ff eb  bl   0x8036f4f0        ; ★ 算法 1 路径
0036f664  30 00 94 e5  ldr  r0, [r4, #0x30]
0036f668  10 80 bd e8  pop  {r4, pc}
```

**关键点：这个 1 不落盘到任何字段**。它只在寄存器里活 2 条指令，随后被 `0x36f634` 消费。
因此"目标字段偏移"这个问题在原始实现里不成立——没有 `struct->algo`，只有实参。这一点对后续 mod 很重要：
不存在"改结构体初始值"的旁路，只有这一个 `mov`。

### 1.4 算法 0 与算法 1 的真实差别

**算法 0（`0x36f13c`）**

```
0036f13c  push {r4,r5,lr}; mov r4,r0; sub sp,#0xc; mov r5,r1
0036f14c  bl 0x8036e6d4        ; ① 采集历史样本（最多 [obj+0x10]=3 帧）
0036f158  bl 0x8036e7bc        ; ② calculatePredictTime()
0036f15c  ldr r3,[r4,#0x28]    ; ③ 样本数
0036f160  str r0,[r4,#0x24]    ;    预测时间
0036f164  cmp r3,#1
0036f168  ble 0x8036f1bc       ;    1 帧 → 直接用第 0 帧位置
0036f16c  cmp r3,#2
0036f170  mov r0,r4
0036f174  beq 0x8036f1cc       ;    2 帧 → 0x36ea08 (MOP，一阶)
0036f178  bl 0x8036ecc4        ; ★  3 帧 → SECOND PREDICT（二阶）  [0x36f1cc 的上一支]
0036f17c  mov r1,r0
0036f180  str r0,[r4,#0x2c]
0036f184  mov r0,r4
0036f188  bl 0x8036efe8       ;    最终位置 → [obj+0x30]
0036f18c  str r0,[r4,#0x30]
...
0036f1bc  ldr r3,[r4,#4]; ldr r1,[r3]; str r1,[r4,#0x2c]   ; 单样本
0036f1cc  bl 0x8036ea08                                    ; MOP
```

**算法 1（`0x36f4f0`）**

```
0036f4f0  push {r4,r5,lr}; mov r2,#1; mov r4,r0; sub sp,#0xc; mov r5,r1
0036f504  ldr r0,[pc,#0x118] / ldr r1,[pc,#0x118] / bl 0x8034ccf4   ; trace_begin
0036f510  ldr r1,[r4,#0x20]
0036f514  mov r0,r4
0036f518  bl 0x8036f418        ; ① 只处理 1 帧，把样本数压到 0/1/2
0036f51c  ldr r1,[r4,#0xc]     ;    上一次时间戳
0036f520  mov r0,r4
0036f524  rsb r1,r1,r5         ;    Δt = 目标时刻 - last
0036f528  bl 0x8036e7bc        ; ② calculatePredictTime(Δt)
0036f52c  ldr r3,[r4,#0xc]
0036f534  str r0,[r4,#0x24]
...
0036f554  ldr r3,[r4,#0x28]    ; ③ 样本数 ∈ {0,1,2}
0036f558  cmp r3,#0
0036f55c  ble 0x8036f5fc       ;    0 → 0
0036f560  cmp r3,#1
0036f564  beq 0x8036f5c8       ;    1 → 取第 0 帧位置
0036f568  mov r0,r4
0036f56c  bl 0x8036f474        ;    2 → 校验索引窗口
0036f578  bl 0x8036ea08        ;    ★ 只有 MOP（一阶线性），没有 0x36ecc4
```

**结论（可静态证实）**

| | 算法 0 | 算法 1 |
|---|---|---|
| 采样帧数上限 | 3（`0x36f690: mov r1,#3; str r1,[r0,#0x10]`） | 2（`0x36f418` 只会写 0/1/2） |
| 3 帧时 | `0x36ecc4` SECOND PREDICT（含 `0.5·a·t²`，见 `0x36ee44 movt r3,#0x3fe0` = double 0.5） | **不可达** |
| 2 帧时 | `0x36ea08` MOP（一阶） | `0x36ea08` MOP（一阶） |
| 位置来源 | 全局 AF 结果环形缓冲（每帧自动写入） | 同一缓冲，但只取 1 个索引 |
| 时间基准 | 直接用目标时刻 `r5` | `r5 - [obj+0xc]` |

即：**算法 1 在结构上不可能进入二阶分支**，这与"运动主体跟不上"的现象一致——一阶线性外推对加速运动必然滞后一个 `½at²` 的偏差量。

### 1.5 算法 0 需要的额外输入 —— 是否已存在

`0x36e6d4`（样本采集）反汇编：

```
0036e6d4  push {r4,r5,lr}; mov r4,#0; sub sp,#0x24; mov r5,r0
0036e6f0  bl 0x8034b1f8           ; 取 AF 结果环形缓冲单例
0036e6f4  bl 0x8034b41c           ; count = ring->[0x18]
0036e6f8  ldr r3,[r5,#0x10]       ; maxSamples = 3
0036e6fc  cmp r0,r3
0036e700  strle r0,[r5,#0x28]     ; sampleCount = min(count, 3)
0036e704  strgt r3,[r5,#0x28]
...
0036e730  bl 0x8034b1f8
0036e734  add r1,sp,#0x14 / add r2,sp,#0x18 / add r3,sp,#0x1c
0036e740  str r4,[sp]             ; 第 5 参 = 索引 i
0036e744  bl 0x8034b2e4           ; getSample(i, &x, &y, &dist)
0036e748  ldm r5,{r2,r3}          ; obj[0]=x数组, obj[4]=y数组
0036e74c  ldr r1,[sp,#0x14]
0036e750  str r1,[r2,r4,lsl #2]   ; x[i]
0036e754  ldr r2,[sp,#0x18]
0036e758  str r2,[r3,r4,lsl #2]   ; y[i]
0036e75c  ldr r2,[sp,#0x1c]
0036e760  ldr r3,[r5,#8]
0036e764  str r2,[r3,r4,lsl #2]   ; dist[i]
```

辅助函数：

| 地址 | 语义 | 证据 |
|---|---|---|
| `0x34b1f8` | AF 结果环形缓冲单例 getter | `tst r3,#1` + `__cxa_guard` 模式 |
| `0x34b41c` | `getCount()` = `[ring+0x18]` | `ldr r0,[r0,#0x18]; bx lr` |
| `0x34b2e4` | `getSample(i, &x, &y, &dist)`，环形长度 `[ring+0x10]`，三数组 `ring+4/+8/+0xc` | 见上 |

预测器对象构造（`0x36f68c`）：

```
0036f68c  mov r1,#3 ; str r1,[r0,#0x10]     ; maxSamples = 3
0036f694  movw r2,#0x93e0 ; movt r2,#4 ; str r2,[r0,#0x20]
0036f69c  movw r1,#0xd6d8 ; str r1,[r0,#0x14]
0036f6ac  movw r1,#0x2710 ; str r1,[r0,#0x1c]   ; 10000
0036f6b4  str r1,[r0,#0x1c]
0036f6b8  str r2,[r0,#0x20]
0036f6bc..6d0  [0xc]=[0x18]=[0x24]=[0x28]=[0x2c]=[0x30]=0
0036f6d4  mov r0,#0xc    ; bl malloc → [r0]   (3 个 int 的 x 数组)
0036f6e4  lsl r0,r3,#2   ; bl malloc → [r0+4] (y 数组)
0036f6f4  lsl r0,r3,#2   ; bl malloc → [r0+8] (dist 数组)
```

**判定：二阶所需的"上上帧位置"数据已经存在。**
- 三帧位置/评价距离来自**全局共享**的 AF 结果环形缓冲，与算法选择无关，每帧都在写；
- 预测器对象在构造时已按 `maxSamples=3` 分配了 3 槽 × 2 数组 + 时间字段；
- 算法 0 与算法 1 写的是**同一组输出字段**（`+0x24 / +0x28 / +0x2c / +0x30`），无布局冲突。

→ **改常量即可，不需要新增状态、不需要额外写代码。**

### 1.6 改动方案

```
文件偏移 0x33CFF0
-  01 10 a0 e3      mov r1, #1     (E3A01001)
+  00 10 a0 e3      mov r1, #0     (E3A01000)
```

单点、4 字节、可逆。同一函数末尾的 `bl 0x33bf28`（应用镜头）与返回约定完全不变。

### 1.7 风险评估

| 项 | 评估 |
|---|---|
| 数据完整性 | 低风险。三帧样本由全局缓冲提供，`sampleCount` 会被重算，不存在读脏 |
| 冷启动 | **中**。刚进入 Conti-H 的 1~2 帧 `sampleCount` 仍 < 3，会退化到 MOP/单点，与原来等价；从第 3 帧起才真正二阶 |
| 时间基准差异 | **中**。算法 1 用 `r5 - [obj+0xc]`，算法 0 直接用 `r5`。`[obj+0xc]` 在算法 0 下不再更新（`0x36f474` 是唯一写入者），但全局检索未发现其它读者，故只是"少写一个字段"，不影响他处 |
| 过冲 | **中**。二阶项是 `0.5·a·t²`，对 AF 评价距离的非线性抖动（对比度穿越、遮挡）会放大。建议同时观察 `0x36ea08`(MOP) 与 `0x36ecc4`(2nd) 的切换频度 |
| 未覆盖的另一入口 | `0x33c044` = `ExecuteExposurePredictContiNFocus` **同样有一条 `mov r1,#1`（@ `0x33c068`）**，但它调的是 `0x3706c4`（另一套实现），不能靠改一处覆盖两条路径 |
| 未验证 | 算法 0 是否被其它机型/模式实际使用过（若从未启用，属于死代码，风险上调）。需要动态确认 `0x36ecc4` 是否曾被命中 |

---

## 二、800ms 双常量拆分方案

### 2.1 函数边界

`0x2f6c48` **就是函数起点**（`push {r4,r5,r6,r7,r8,lr}` = `E92D41F0`），
结束于 `0x2f6e48`（`b 0x2f6dbc`，唯一出口 `0x2f6dbc` 的 `pop {...,pc}`）。
常量池 `0x2f6e4c – 0x2f6e64`（不属于代码）。

- `0x2f6e4c = 0x81550408` ← **GAF 基址**
- `0x2f6e50 = ":: UPDATE FD AREA"`
- `0x2f6e54 = 0x816d429c`（trace ctx）
- `0x2f6e58 = ":: FD LOST: ACCEPTED(%d)."`
- `0x2f6e5c = ":: FD LOST: WAIT STABLE TIME (%d)."`
- `0x2f6e60 = ":: FD DETECTED: WAIT STABLE TIME(%d)."`
- `0x2f6e64 = ":: FIX FD AREA FOR CRAF."`

调用者：**仅 1 处** —— `0x2f7730`（`mov r0,r5; bl 0x802f6c48`），返回 0/1 用于判断"本帧人脸框是否有效"，
`subs r1,r0,#0; beq 0x802f791c` 直接分叉。

### 2.2 完整反汇编（`0x2f6c48 – 0x2f6e48`，128 条）

```
002f6c48  f0 41 2d e9  push   {r4, r5, r6, r7, r8, lr}
002f6c4c  00 50 a0 e3  mov    r5, #0
002f6c50  20 d0 4d e2  sub    sp, sp, #0x20
002f6c54  00 70 a0 e1  mov    r7, r0                  ; r7 = 调用方对象
002f6c58  08 50 8d e5  str    r5, [sp, #8]            ; 6 个字的出参缓冲清零
002f6c5c  0c 50 8d e5  str    r5, [sp, #0xc]
002f6c60  10 50 8d e5  str    r5, [sp, #0x10]
002f6c64  14 50 8d e5  str    r5, [sp, #0x14]
002f6c68  18 50 8d e5  str    r5, [sp, #0x18]
002f6c6c  1c 50 8d e5  str    r5, [sp, #0x1c]
002f6c70  51 25 ff eb  bl     0x802c01bc              ; 取 AF 算法单例
002f6c74  d0 41 9f e5  ldr    r4, [pc, #0x1d0]        ; r4 = GAF = 0x81550408
002f6c78  00 60 a0 e1  mov    r6, r0                  ; r6 = 算法单例
002f6c7c  04 30 d4 e5  ldrb   r3, [r4, #4]            ; GAF.flags
002f6c80  5c 20 d4 e5  ldrb   r2, [r4, #0x5c]         ; GAF.stage
002f6c84  14 30 03 e2  and    r3, r3, #0x14
002f6c88  02 00 52 e3  cmp    r2, #2                  ; stage == S0-AF ?
002f6c8c  32 8e a0 13  movne  r8, #0x320              ; ★ 800 ms（非 S0-AF）
002f6c90  05 80 a0 01  moveq  r8, r5                  ;   stage==2 → 0
002f6c94  14 00 53 e3  cmp    r3, #0x14
002f6c98  00 50 8d e5  str    r5, [sp]
002f6c9c  04 50 8d e5  str    r5, [sp, #4]
002f6ca0  5e 00 00 0a  beq    0x802f6e20              ; 两个 flag 都置位 → FIX FD AREA FOR CRAF
002f6ca4  00 30 90 e5  ldr    r3, [r0]
002f6ca8  44 32 93 e5  ldr    r3, [r3, #0x244]        ; vfn: getFaceCount()
002f6cac  33 ff 2f e1  blx    r3
002f6cb0  00 50 50 e2  subs   r5, r0, #0
002f6cb4  30 00 00 da  ble    0x802f6d7c              ; 无人脸 → 丢失分支
002f6cb8  00 30 96 e5  ldr    r3, [r6]
002f6cbc  06 00 a0 e1  mov    r0, r6
002f6cc0  40 c2 93 e5  ldr    ip, [r3, #0x240]        ; vfn: getFaceArea(&buf[8], &sp[0], &sp[4])
002f6cc4  08 10 8d e2  add    r1, sp, #8
002f6cc8  0d 20 a0 e1  mov    r2, sp
002f6ccc  04 30 8d e2  add    r3, sp, #4
002f6cd0  3c ff 2f e1  blx    ip
002f6cd4  00 00 50 e3  cmp    r0, #0
002f6cd8  27 00 00 0a  beq    0x802f6d7c              ; 取框失败 → 丢失分支
002f6cdc  04 60 d4 e5  ldrb   r6, [r4, #4]
002f6ce0  04 60 06 e2  and    r6, r6, #4              ; 已有 FD 区域标志
002f6ce4  76 60 ef e6  uxtb   r6, r6
002f6ce8  00 00 56 e3  cmp    r6, #0
002f6cec  04 00 00 1a  bne    0x802f6d04              ; 已有框 → 直接更新
002f6cf0  db 98 07 eb  bl     0x804dd064             ; now = 0x2ba9c()/1000  (ms)
002f6cf4  a0 20 94 e5  ldr    r2, [r4, #0xa0]        ; GAF.lastUpdate
002f6cf8  00 20 62 e0  rsb    r2, r2, r0             ; delta = now - lastUpdate
002f6cfc  02 00 58 e1  cmp    r8, r2                 ; ★ 获得方向
002f6d00  41 00 00 ca  bgt    0x802f6e0c             ;   r8 > delta → 还在稳定期，继续等
002f6d04  04 30 d4 e5  ldrb   r3, [r4, #4]           ; ── 接受新框 ──
002f6d08  03 20 a0 e3  mov    r2, #3
002f6d0c  04 30 83 e3  orr    r3, r3, #4
002f6d10  1f 32 c4 e7  bfc    r3, #4, #1
002f6d14  e8 2a 84 e5  str    r2, [r4, #0xae8]       ; GAF.state = 3
002f6d18  04 30 c4 e5  strb   r3, [r4, #4]
002f6d1c  c8 5a 84 e5  str    r5, [r4, #0xac8]       ; 人数
002f6d20  cf 98 07 eb  bl     0x804dd064
002f6d24  08 30 9d e5  ldr    r3, [sp, #8]
002f6d28  a0 00 84 e5  str    r0, [r4, #0xa0]        ; lastUpdate = now
002f6d2c  cc 3a 84 e5  str    r3, [r4, #0xacc]       ; 6 字框数据
002f6d30  0c 30 9d e5  ldr    r3, [sp, #0xc]
002f6d34  14 11 9f e5  ldr    r1, [pc, #0x114]
002f6d38  d0 3a 84 e5  str    r3, [r4, #0xad0]
002f6d3c  10 30 9d e5  ldr    r3, [sp, #0x10]
002f6d40  0c 01 9f e5  ldr    r0, [pc, #0x10c]
002f6d44  d4 3a 84 e5  str    r3, [r4, #0xad4]
002f6d48  14 30 9d e5  ldr    r3, [sp, #0x14]
002f6d4c  d8 3a 84 e5  str    r3, [r4, #0xad8]
002f6d50  18 30 9d e5  ldr    r3, [sp, #0x18]
002f6d54  dc 3a 84 e5  str    r3, [r4, #0xadc]
002f6d58  1c 30 9d e5  ldr    r3, [sp, #0x1c]
002f6d5c  e0 3a 84 e5  str    r3, [r4, #0xae0]
002f6d60  00 30 9d e5  ldr    r3, [sp]
002f6d64  e4 3a 84 e5  str    r3, [r4, #0xae4]
002f6d68  26 57 01 eb  bl     0x8034ca08             ; log ":: UPDATE FD AREA"
002f6d6c  07 00 a0 e1  mov    r0, r7
002f6d70  5a ff ff eb  bl     0x802f6ae0             ; 应用/下发 FD 框
002f6d74  01 30 a0 e3  mov    r3, #1
002f6d78  0f 00 00 ea  b      0x802f6dbc
002f6d7c  04 30 d4 e5  ldrb   r3, [r4, #4]           ; ── 丢失分支入口 ──
002f6d80  c4 50 9f e5  ldr    r5, [pc, #0xc4]        ; r5 = GAF（与 r4 同值，冗余）
002f6d84  04 30 03 e2  and    r3, r3, #4
002f6d88  73 30 ef e6  uxtb   r3, r3
002f6d8c  00 00 53 e3  cmp    r3, #0
002f6d90  0c 00 00 1a  bne    0x802f6dc8             ; 之前有框 → 去抖
002f6d94  04 30 d4 e5  ldrb   r3, [r4, #4]           ; ── 本就无框：清空返回 0 ──
002f6d98  00 50 a0 e3  mov    r5, #0
002f6d9c  fb 30 03 e2  and    r3, r3, #0xfb
002f6da0  15 32 c4 e7  bfi    r3, r5, #4, #1
002f6da4  04 30 c4 e5  strb   r3, [r4, #4]
002f6da8  e8 5a 84 e5  str    r5, [r4, #0xae8]
002f6dac  c8 5a 84 e5  str    r5, [r4, #0xac8]
002f6db0  ab 98 07 eb  bl     0x804dd064
002f6db4  05 30 a0 e1  mov    r3, r5
002f6db8  a0 00 84 e5  str    r0, [r4, #0xa0]
002f6dbc  03 00 a0 e1  mov    r0, r3                 ; ── 唯一出口 ──
002f6dc0  20 d0 8d e2  add    sp, sp, #0x20
002f6dc4  f0 81 bd e8  pop    {r4, r5, r6, r7, r8, pc}
002f6dc8  a5 98 07 eb  bl     0x804dd064             ; now
002f6dcc  a0 20 95 e5  ldr    r2, [r5, #0xa0]        ; lastUpdate
002f6dd0  00 20 62 e0  rsb    r2, r2, r0             ; delta
002f6dd4  02 00 58 e1  cmp    r8, r2                 ; ★ 丢失方向（同一 r8、同一常量）
002f6dd8  74 00 9f e5  ldr    r0, [pc, #0x74]        ; trace ctx
002f6ddc  02 00 00 ca  bgt    0x802f6dec             ;   r8 > delta → 保持旧框
002f6de0  70 10 9f e5  ldr    r1, [pc, #0x70]       ; "FD LOST: ACCEPTED(%d)."
002f6de4  07 57 01 eb  bl     0x8034ca08
002f6de8  e9 ff ff ea  b      0x802f6d94             ;   接受丢失 → 清框返回 0
002f6dec  68 10 9f e5  ldr    r1, [pc, #0x68]       ; "FD LOST: WAIT STABLE TIME (%d)."
002f6df0  04 57 01 eb  bl     0x8034ca08
002f6df4  03 30 a0 e3  mov    r3, #3
002f6df8  07 00 a0 e1  mov    r0, r7
002f6dfc  e8 3a 85 e5  str    r3, [r5, #0xae8]       ; state = 3（保持）
002f6e00  36 ff ff eb  bl     0x802f6ae0
002f6e04  01 30 a0 e3  mov    r3, #1
002f6e08  eb ff ff ea  b      0x802f6dbc             ;   返回 1（继续用旧框）
002f6e0c  40 00 9f e5  ldr    r0, [pc, #0x40]
002f6e10  48 10 9f e5  ldr    r1, [pc, #0x48]       ; "FD DETECTED: WAIT STABLE TIME(%d)."
002f6e14  fb 56 01 eb  bl     0x8034ca08
002f6e18  06 30 a0 e1  mov    r3, r6
002f6e1c  e6 ff ff ea  b      0x802f6dbc             ;   r6==0 → 返回 0（本帧不认新框）
002f6e20  06 30 a0 e3  mov    r3, #6                 ; ── FIX FD AREA FOR CRAF ──
002f6e24  e8 3a 84 e5  str    r3, [r4, #0xae8]
002f6e28  8d 98 07 eb  bl     0x804dd064
002f6e2c  30 10 9f e5  ldr    r1, [pc, #0x30]
002f6e30  a0 00 84 e5  str    r0, [r4, #0xa0]
002f6e34  18 00 9f e5  ldr    r0, [pc, #0x18]
002f6e38  f2 56 01 eb  bl     0x8034ca08
002f6e3c  07 00 a0 e1  mov    r0, r7
002f6e40  26 ff ff eb  bl     0x802f6ae0
002f6e44  01 30 a0 e3  mov    r3, #1
002f6e48  eb ff ff ea  b      0x802f6dbc
```

### 2.3 两个比较点 ±20 条

**获得路径（`0x2f6cfc`）**

```
002f6ca4  00 30 90 e5  ldr    r3, [r0]
002f6ca8  44 32 93 e5  ldr    r3, [r3, #0x244]     ; getFaceCount
002f6cac  33 ff 2f e1  blx    r3
002f6cb0  00 50 50 e2  subs   r5, r0, #0
002f6cb4  30 00 00 da  ble    0x802f6d7c
002f6cb8  00 30 96 e5  ldr    r3, [r6]
002f6cbc  06 00 a0 e1  mov    r0, r6
002f6cc0  40 c2 93 e5  ldr    ip, [r3, #0x240]     ; getFaceArea
002f6cc4  08 10 8d e2  add    r1, sp, #8
002f6cc8  0d 20 a0 e1  mov    r2, sp
002f6ccc  04 30 8d e2  add    r3, sp, #4
002f6cd0  3c ff 2f e1  blx    ip
002f6cd4  00 00 50 e3  cmp    r0, #0
002f6cd8  27 00 00 0a  beq    0x802f6d7c
002f6cdc  04 60 d4 e5  ldrb   r6, [r4, #4]
002f6ce0  04 60 06 e2  and    r6, r6, #4
002f6ce4  76 60 ef e6  uxtb   r6, r6
002f6ce8  00 00 56 e3  cmp    r6, #0
002f6cec  04 00 00 1a  bne    0x802f6d04
002f6cf0  db 98 07 eb  bl     0x804dd064           ; now(ms)
002f6cf4  a0 20 94 e5  ldr    r2, [r4, #0xa0]
002f6cf8  00 20 62 e0  rsb    r2, r2, r0
002f6cfc  02 00 58 e1  cmp    r8, r2               ; ◎
002f6d00  41 00 00 ca  bgt    0x802f6e0c
002f6d04  04 30 d4 e5  ldrb   r3, [r4, #4]         ; 接受
002f6d08  03 20 a0 e3  mov    r2, #3
002f6d0c  04 30 83 e3  orr    r3, r3, #4
002f6d10  1f 32 c4 e7  bfc    r3, #4, #1
002f6d14  e8 2a 84 e5  str    r2, [r4, #0xae8]
002f6d18  04 30 c4 e5  strb   r3, [r4, #4]
002f6d1c  c8 5a 84 e5  str    r5, [r4, #0xac8]
002f6d20  cf 98 07 eb  bl     0x804dd064
002f6d24  08 30 9d e5  ldr    r3, [sp, #8]
```

**丢失路径（`0x2f6dd4`）**

```
002f6d7c  04 30 d4 e5  ldrb   r3, [r4, #4]
002f6d80  c4 50 9f e5  ldr    r5, [pc, #0xc4]       ; r5 := GAF（= r4，冗余）
002f6d84  04 30 03 e2  and    r3, r3, #4
002f6d88  73 30 ef e6  uxtb   r3, r3
002f6d8c  00 00 53 e3  cmp    r3, #0
002f6d90  0c 00 00 1a  bne    0x802f6dc8
002f6d94  04 30 d4 e5  ldrb   r3, [r4, #4]
002f6d98  00 50 a0 e3  mov    r5, #0
002f6d9c  fb 30 03 e2  and    r3, r3, #0xfb
002f6da0  15 32 c4 e7  bfi    r3, r5, #4, #1
002f6da4  04 30 c4 e5  strb   r3, [r4, #4]
002f6da8  e8 5a 84 e5  str    r5, [r4, #0xae8]
002f6dac  c8 5a 84 e5  str    r5, [r4, #0xac8]
002f6db0  ab 98 07 eb  bl     0x804dd064
002f6db4  05 30 a0 e1  mov    r3, r5
002f6db8  a0 00 84 e5  str    r0, [r4, #0xa0]
002f6dbc  03 00 a0 e1  mov    r0, r3
002f6dc0  20 d0 8d e2  add    sp, sp, #0x20
002f6dc4  f0 81 bd e8  pop    {r4, r5, r6, r7, r8, pc}
002f6dc8  a5 98 07 eb  bl     0x804dd064
002f6dcc  a0 20 95 e5  ldr    r2, [r5, #0xa0]
002f6dd0  00 20 62 e0  rsb    r2, r2, r0
002f6dd4  02 00 58 e1  cmp    r8, r2               ; ◎
002f6dd8  74 00 9f e5  ldr    r0, [pc, #0x74]
002f6ddc  02 00 00 ca  bgt    0x802f6dec
002f6de0  70 10 9f e5  ldr    r1, [pc, #0x70]
002f6de4  07 57 01 eb  bl     0x8034ca08
002f6de8  e9 ff ff ea  b      0x802f6d94
002f6dec  68 10 9f e5  ldr    r1, [pc, #0x68]
002f6df0  04 57 01 eb  bl     0x8034ca08
002f6df4  03 30 a0 e3  mov    r3, #3
002f6df8  07 00 a0 e1  mov    r0, r7
002f6dfc  e8 3a 85 e5  str    r3, [r5, #0xae8]
002f6e00  36 ff ff eb  bl     0x802f6ae0
002f6e04  01 30 a0 e3  mov    r3, #1
002f6e08  eb ff ff ea  b      0x802f6dbc
```

语义确认（与日志字符串逐条对齐）：
- 获得方向：`delta ≥ r8` → 接受新框；`delta < r8` → "FD DETECTED: WAIT STABLE TIME"，返回 0。
- 丢失方向：`delta ≥ r8` → "FD LOST: ACCEPTED"，清框返回 0；`delta < r8` → "FD LOST: WAIT STABLE TIME"，保持旧框返回 1。

### 2.4 丢失路径的空闲寄存器

| 寄存器 | 在 `0x2f6dc8 – 0x2f6df0` 的状态 |
|---|---|
| `r0` | 时间戳（`rsb` 源），`0x2f6dd8` 后被日志字符串覆盖 |
| `r1` | **空闲**，`0x2f6de0/0x2f6dec` 才被写入 |
| `r2` | delta（`cmp` 的操作数，必须保留） |
| `r3` | **空闲**，`0x2f6df4` 才被写 `#3` |
| `r4` | GAF（后续 `0x2f6d94` 要用） |
| `r5` | GAF（同值冗余） |
| `r6` | **完全空闲**（只在本路径之外的 `0x2f6cdc`/`0x2f6e18` 使用） |
| `r7` | 入参对象（`0x2f6df8`/`0x2f6e3c` 要用） |
| `r8` | 阈值（不可动） |
| `r9–r11` | **不可用**：`push {r4,r5,r6,r7,r8,lr}` 未保存，使用会破坏调用者 |

**可复用的是 `r6`**（callee-saved 且已保存，此路径内 dead）。

同时注意一处冗余：`0x2f6d80` 的 `ldr r5,[pc,#0xc4]` 与 `0x2f6c74` 的 `ldr r4,...` 取的是**同一个常量池槽** `0x2f6e4c`，且在 `0x2f6c74` 之后 `r4` 从未被改写 —— 所以 `r5 == r4`，这条装载是多余的，可以整条替换。

### 2.5 最小改动方案

#### 方案 A（推荐）：1 条指令，利用 800 = 100 × 8

```
文件偏移 0x2F7DD4
-  02 00 58 e1      cmp r8, r2          (E1580002)
+  82 01 58 e1      cmp r8, r2, lsl #3  (E1580182)
```

等价性证明：
- 丢失阈值：`r8 > delta` ⟺ `800 > delta` ⟺ `800 > 8 · (delta/8)` ⟺ `100 > delta'`（阈值精确变为 100 ms）
- 获得方向完全不动（`0x2f6cfc` 仍是 `cmp r8,r2`，仍是 800 ms）
- **`stage == 2` 时 `r8 = 0`**：`0 > 8·delta` 与 `0 > delta` 对任意 `delta ≥ 0` 同为假 → 行为逐位一致，语义保持
- 溢出边界：`delta > 2^29 ms ≈ 6.2 天` 才会溢出，实际不可达

若目标阈值不是 100 ms，可按 `800 / 2^k` 取：`k=1 → 400ms`、`k=2 → 200ms`、`k=3 → 100ms`、`k=4 → 50ms`。

#### 方案 B（需要任意非 2 的幂阈值时的寄存器版）

利用 `r5` 冗余 + `r6` 空闲，共 4 条：

```
1) 0x2F7D80  ldr r5,[pc,#0xc4]   (E59F50C4) → lsr r6, r8, #3   (E1A061A8)   ; r6 = 800/8 = 100，stage2 时 = 0
2) 0x2F7DCC  ldr r2,[r5,#0xa0]   (E59520A0) → ldr r2,[r4,#0xa0] (E59420A0)
3) 0x2F7DFC  str r3,[r5,#0xae8]  (E5853AE8) → str r3,[r4,#0xae8](E5843AE8)
4) 0x2F7DD4  cmp r8,r2           (E1580002) → cmp r6,r2          (E1560002)
```

（1 把 `r5` 的含义换成阈值、并自动继承 `stage==2 → 0`；2、3 把原来依赖 `r5==GAF` 的两处改用 `r4`，因为 `r5` 已被占用。）
若要 200 ms，把第 1 条换成 `lsr r6, r8, #2`（`E1A06128`）即可，逻辑同构。
若要 300 ms 这类非 2 的幂，需额外 1 个指令槽（`mov r6,#0x12c` 会丢掉 `stage==2 → 0`，必须再补一条 `cmp r8,#0` 分支）→ 建议开代码洞，不再压迫本函数。

### 2.6 影响面

- `0x2f6c48` **只有一个调用者 `0x2f7730`**，参数为 `r0 = 对象指针`（后续传入 `0x2f6ae0` 与 GAF 字段），无第二语义。
- 返回值：`1 = 有可用人脸框（本帧可继续用）`，`0 = 无框`。改常量只改变"判定为有框"的持续时间，不改变返回类型。
- 与本函数共享 `r8` 的只有获得方向；方案 A 只改丢失方向的比较，**获得方向的 800 ms 完全不变**。
- 副作用：人脸短暂丢失（转头、遮挡）时会更快放弃旧框 → 更快回落到"重新检测/其他区域"，可能导致框闪烁。建议先用 200 ms（`k=2`）而不是直接 100 ms。

---

## 三、主脸打分完整算法

### 3.1 `0x143740` 全函数反汇编（`0x143740 – 0x1438d4`）

```
00143740  f0 4f 2d e9  push   {r4, r5, r6, r7, r8, sb, sl, fp, lr}
00143744  9c c3 93 e5  ldr    ip, [r3, #0x39c]        ; n = ctx->faceCount
00143748  bc d0 4d e2  sub    sp, sp, #0xbc
0014374c  00 00 5c e3  cmp    ip, #0
00143750  03 60 a0 e1  mov    r6, r3                  ; r6 = ctx
00143754  14 00 8d e5  str    r0, [sp, #0x14]
00143758  5a 00 00 0a  beq    0x801438c8              ; n==0 → return
0014375c  0a 00 5c e3  cmp    ip, #0xa
00143760  58 00 00 ca  bgt    0x801438c8              ; n>10 → return
00143764  00 00 5c e3  cmp    ip, #0
00143768  40 10 8d d2  addle  r1, sp, #0x40
0014376c  08 10 8d d5  strle  r1, [sp, #8]
00143770  18 80 8d d2  addle  r8, sp, #0x18
00143774  2d 00 00 da  ble    0x80143830
00143778  a1 1f 81 e0  add    r1, r1, r1, lsr #31    ; r1 /= 2（朝零取整）
0014377c  a2 2f 82 e0  add    r2, r2, r2, lsr #31    ; r2 /= 2
00143780  00 50 a0 e3  mov    r5, #0
00143784  c1 10 a0 e1  asr    r1, r1, #1
00143788  c2 20 a0 e1  asr    r2, r2, #1
0014378c  03 40 a0 e1  mov    r4, r3
00143790  40 30 8d e2  add    r3, sp, #0x40
00143794  0c 10 8d e5  str    r1, [sp, #0xc]          ; cx = W/2
00143798  10 20 8d e5  str    r2, [sp, #0x10]         ; cy = H/2
0014379c  05 b0 a0 e1  mov    fp, r5
001437a0  08 30 8d e5  str    r3, [sp, #8]            ; area[] = sp+0x40
001437a4  18 80 8d e2  add    r8, sp, #0x18           ; dist[] = sp+0x18
001437a8  08 20 94 e5  ldr    r2, [r4, #8]            ; ── 逐脸循环 ──
001437ac  0c 30 94 e5  ldr    r3, [r4, #0xc]
001437b0  04 e0 94 e5  ldr    lr, [r4, #4]
001437b4  00 00 94 e5  ldr    r0, [r4]                ; x, y, w, h（stride 0x14）
001437b8  a2 cf 82 e0  add    ip, r2, r2, lsr #31
001437bc  93 02 02 e0  mul    r2, r3, r2              ; area = w*h
001437c0  0c 10 9d e5  ldr    r1, [sp, #0xc]
001437c4  a3 3f 83 e0  add    r3, r3, r3, lsr #31
001437c8  cc c0 80 e0  add    ip, r0, ip, asr #1      ; fcx = x + w/2
001437cc  c3 70 8e e0  add    r7, lr, r3, asr #1      ; fcy = y + h/2
001437d0  08 30 9d e5  ldr    r3, [sp, #8]
001437d4  0c 90 61 e0  rsb    sb, r1, ip              ; dx = fcx - cx
001437d8  05 20 83 e7  str    r2, [r3, r5]            ; area[i] = w*h
001437dc  09 00 a0 e1  mov    r0, sb
001437e0  62 11 0f eb  bl     0x80507d70              ; abs(dx)
001437e4  00 a0 a0 e1  mov    sl, r0
001437e8  09 00 a0 e1  mov    r0, sb
001437ec  5f 11 0f eb  bl     0x80507d70              ; abs(dx)
001437f0  10 10 9d e5  ldr    r1, [sp, #0x10]
001437f4  90 0a 0a e0  mul    sl, r0, sl              ; |dx|·|dx| = dx²
001437f8  07 70 61 e0  rsb    r7, r1, r7              ; dy = fcy - cy
001437fc  07 00 a0 e1  mov    r0, r7
00143800  5a 11 0f eb  bl     0x80507d70
00143804  00 90 a0 e1  mov    sb, r0
00143808  07 00 a0 e1  mov    r0, r7
0014380c  57 11 0f eb  bl     0x80507d70
00143810  90 a9 20 e0  mla    r0, r0, sb, sl          ; dist² = dx² + dy²
00143814  9c c3 96 e5  ldr    ip, [r6, #0x39c]
00143818  01 b0 8b e2  add    fp, fp, #1
0014381c  0b 00 5c e1  cmp    ip, fp
00143820  05 00 88 e7  str    r0, [r8, r5]            ; dist[i] = dx²+dy²
00143824  14 40 84 e2  add    r4, r4, #0x14           ; next face
00143828  04 50 85 e2  add    r5, r5, #4
0014382c  dd ff ff ca  bgt    0x801437a8
00143830  00 a0 a0 e3  mov    sl, #0
00143834  68 40 8d e2  add    r4, sp, #0x68           ; distRank[]
00143838  08 10 a0 e1  mov    r1, r8                  ; dist[]
0014383c  0c 20 a0 e1  mov    r2, ip                  ; n
00143840  14 00 9d e5  ldr    r0, [sp, #0x14]
00143844  04 30 a0 e1  mov    r3, r4
00143848  00 a0 8d e5  str    sl, [sp]                ; 第5参 = 0
0014384c  6e ff ff eb  bl     0x8014360c              ; rank(dist, n, distRank, 0)
00143850  90 50 8d e2  add    r5, sp, #0x90           ; areaRank[]
00143854  01 30 a0 e3  mov    r3, #1
00143858  9c 23 96 e5  ldr    r2, [r6, #0x39c]
0014385c  14 00 9d e5  ldr    r0, [sp, #0x14]
00143860  00 30 8d e5  str    r3, [sp]                ; 第5参 = 1
00143864  08 10 9d e5  ldr    r1, [sp, #8]            ; area[]
00143868  05 30 a0 e1  mov    r3, r5
0014386c  66 ff ff eb  bl     0x8014360c              ; rank(area, n, areaRank, 1)
00143870  9c 83 96 e5  ldr    r8, [r6, #0x39c]        ; n
00143874  0a 00 58 e1  cmp    r8, sl
00143878  14 00 00 da  ble    0x801438d0
0014387c  0a 20 a0 e1  mov    r2, sl                  ; r2 = i*4
00143880  0a 00 a0 e1  mov    r0, sl                  ; best = 0
00143884  0a 70 a0 e1  mov    r7, sl                  ; bestIdx = 0
00143888  0a 10 a0 e1  mov    r1, sl                  ; i = 0
0014388c  02 c0 95 e7  ldr    ip, [r5, r2]            ; areaRank[i]
00143890  02 30 94 e7  ldr    r3, [r4, r2]            ; distRank[i]
00143894  8c c0 8c e0  add    ip, ip, ip, lsl #1      ; ip = areaRank*3
00143898  83 30 83 e0  add    r3, r3, r3, lsl #1      ; r3 = distRank*3
0014389c  83 c0 8c e0  add    ip, ip, r3, lsl #1      ; ip = areaRank*3 + distRank*6  ★6:3
001438a0  00 00 5c e1  cmp    ip, r0
001438a4  71 30 ef e6  uxtb   r3, r1
001438a8  01 10 81 e2  add    r1, r1, #1
001438ac  03 70 a0 81  movhi  r7, r3                  ; 严格更大 → 更新索引
001438b0  00 00 5c e1  cmp    ip, r0                  ; （与 0x1438a0 重复）
001438b4  0c 00 a0 21  movhs  r0, ip                  ; >= → 更新最大值
001438b8  08 00 51 e1  cmp    r1, r8
001438bc  04 20 82 e2  add    r2, r2, #4
001438c0  f1 ff ff 1a  bne    0x8014388c
001438c4  98 73 86 e5  str    r7, [r6, #0x398]        ; ctx->mainFaceIndex = bestIdx
001438c8  bc d0 8d e2  add    sp, sp, #0xbc
001438cc  f0 8f bd e8  pop    {r4, r5, r6, r7, r8, sb, sl, fp, pc}
001438d0  0a 70 a0 e1  mov    r7, sl                  ; n<=0 → index = 0
001438d4  fa ff ff ea  b      0x801438c4
```

### 3.2 名次函数 `0x14360c`

签名（按实际寄存器用法）：`rank(const int *keys /*r1*/, int n /*r2*/, int *out /*r3*/, int dir /*[sp]*/)`
（`r0` 未使用；5 参在栈上，被 `ldrb r3,[sp,#0x38]` 取出）

结构：
1. `0x143650` 起：`if (n-1 > 9) return;`（**最多 10 张脸**）
2. `0x14361c-0x14364c`：在栈上构造恒等索引数组 `idx[0..9] = {0,1,...,9}`
   （`str ip,[sp,#0xc]` = `0x03020100`，`str r0,[sp,#0x10]` = `0x07060504`，`strb #8`、`strb #9`）
3. `0x143658-0x1436c8`：**冒泡排序**。比较 `keys[idx[i]]` 与 `keys[idx[i+1]]`，
   `cmp r5,r6 ; bhs 0x1436b4` —— 只有 `key[i] < key[i+1]` 才交换 ⇒ **降序**（最大在前）；
   相等时 `bhs` 直接跳过 ⇒ **稳定排序**，同分保持原始人脸顺序。同时把 key 数组本身也原地排序。
4. `0x1436d0-0x143734`：按 `dir` 反序编号
   - `dir == 0`：`out[idx[j]] = j+1`
   - `dir != 0`：`out[idx[j]] = n - j`

### 3.3 还原成伪代码

```c
// 入参：a0 = 上下文(未用)，W = 图像宽，H = 图像高，ctx = 人脸上下文
// ctx->faceCount  @ +0x39C
// ctx->mainFaceIndex @ +0x398
// 人脸记录 stride = 0x14：+0 x, +4 y, +8 w, +c h, +10 (他处使用)
void DecideMainFace(void *a0, int W, int H, FaceCtx *ctx)
{
    int n = ctx->faceCount;
    if (n == 0 || n > 10) return;              // 0x14374c / 0x14375c

    int cx = W / 2;                            // 0x143778-0x143794（朝零取整）
    int cy = H / 2;                            // 0x14377c-0x143798

    int area[10], dist[10], distRank[10], areaRank[10];

    for (int i = 0; i < n; i++) {
        int x = f[i].x, y = f[i].y, w = f[i].w, h = f[i].h;
        int fcx = x + w / 2;                   // 0x1437c8
        int fcy = y + h / 2;                   // 0x1437cc
        int dx  = fcx - cx;
        int dy  = fcy - cy;
        dist[i] = dx * dx + dy * dy;           // 0x1437d4-0x143810：欧氏 **平方**，单位 px²
        area[i] = w * h;                       // 0x1437bc
    }

    rank(dist, n, distRank, /*dir=*/0);        // 0x14384c：distRank 大 = 离中心近
    rank(area, n, areaRank, /*dir=*/1);        // 0x14386c：areaRank 大 = 面积大

    int best = 0, bestIdx = 0;
    for (int i = 0; i < n; i++) {
        int s = distRank[i] * 6 + areaRank[i] * 3;   // 0x143894-0x14389c
        if (s > best) { bestIdx = i; }              // 0x1438ac：严格大于才换
        if (s >= best) { best = s; }                // 0x1438b4
    }
    ctx->mainFaceIndex = bestIdx;              // 0x1438c4
}
```

具体回答四问：

1. **中心距离怎么度量**：欧氏距离的**平方**。`dist = (fcx - W/2)² + (fcy - H/2)²`。
   - 不是曼哈顿（有两次乘法 + 加法，不是 `|dx|+|dy|`）。
   - **完全没有归一化**：直接是像素²，范围 `0 … (W/2)²+(H/2)²`。分辨率变化会等比改变数值，
     但因为随后只取**名次**（序关系），归一化对最终主脸选择无影响。
   - `0x80507d70` = `abs()`（`cmp r0,#0; rsblt r0,r0,#0; bx lr`），代码是 `|dx|·|dx| + |dy|·|dy|`，
     结果仍是 `dx² + dy²`（编译器把 `x*x` 编译成两次 `abs` 再乘）。
2. **名次怎么排**：`0x14360c`。冒泡排序（降序）+ 反序编号；
   `dir=0`（距离）时 `out = j+1`，所以 **rank 1 = 最远、rank n = 最近**；
   `dir=1`（面积）时 `out = n-j`，所以 **rank n = 最大面积**。
   两者统一为"数值越大越优"，因此主循环直接取最大。
3. **6:3 权重在哪条指令**：`0x143894`（`areaRank*3`）、`0x143898`（`distRank*3`）、`0x14389c`（`+ distRank*3*2`）——
   合计 `areaRank*3 + distRank*6`。注意**距离权重 6、面积权重 3**，与"中心距离名次 × 6 + 面积名次 × 3"一致。
4. **平滑在哪里做**：**没有**。全函数无 IIR / 无滑动平均 / 无帧间状态读取，
   `ctx->mainFaceIndex` 每帧被直接覆盖（`0x1438c4`）。唯一的帧间状态就是这一个字节，而且它是"结果"不是"输入"。

同分处理两级：
- 名次阶段：稳定排序（相等不交换）→ 同分按原始人脸顺序给名次；
- 选取阶段：`movhi`（严格大于）更新索引、`movhs`（大于等于）更新最大值
  → **并列最优时取索引最小者**，且不会因"后者更优"而抖动（除名次已定序的情况）。

### 3.4 加"上一帧主脸距离惩罚项"的最小改动面

**关键发现：不需要新增全局变量。**
`ctx->mainFaceIndex`（`+0x398`）在 `0x1438c4` 才被写，**在此之前它保存的正是上一帧的主脸索引**。
只要在覆盖前读出来，就能得到"上一帧主脸"。

最小改动面：

| 项 | 需求 |
|---|---|
| 指令槽 | **2 条**：`cmp i, prevIdx` + `addeq score, score, #K` |
| 可用空槽 | **1 个**：`0x1438b0` 的 `cmp ip, r0` 与 `0x1438a0` 完全重复（ARM 的 flags 在 0x1438a4 `uxtb` / 0x1438a8 `add` 中不被修改），可整条复用 |
| 还差 | **1 个 4 字节代码洞**（≥`0x10` 字节即可，需 1 条跳转进/出） |
| 临时寄存器 | `sl`——在打分循环内完全空闲（`sl` 只在 `0x143830` 置 0、`0x143874` 用于 `cmp r8,sl` 后就 dead） |
| 状态存放 | `sp+0xb8`：帧长 `0xbc`，`areaRank[]` 占 `sp+0x90..0xb8`，`sp+0xb8` 恰好空 4 字节，可直接存 `prevIdx` |
| 新增全局 | **不需要** |

推荐实现骨架（`i` 已在 `r3`、`score` 在 `ip`）：

```
; 循环前（用 1 条 trampoline 或就地塞进 0x143880 mov r0,sl 的位置）
    ldr   sl, [r6, #0x398]        ; prevIdx = 上一帧主脸（此时尚未被覆盖）
; 循环内（0x1438b0 的空槽 + 洞内各 1 条）
    cmp   r3, sl
    addeq ip, ip, #18             ; K = 18 = 一个"面积名次档"(3) × 6，可调
```

- 权重 `K` 建议取 `3 … 18`：`18` 等价于"上一帧主脸额外多一次最佳面积名次"，明显但不至于压过中心/面积；
  过大（> 36）会在主体切换时顽固锁死。
- 若不想开代码洞，可退而求其次：**只改两处名次数组**（在 `0x143870` 之后、循环之前给 `distRank[prev] += bias`），
  但同样需要 3~4 条指令，仍需洞；**没有"零额外指令"的加法方案**。
- 风险：本帧引入滞后 1 帧的身份粘性。快速换主体时会晚 1 帧切换；若配合 §2 的 100ms 丢失去抖，
  可能叠加成"该丢不丢"。建议两项不要同时激进。

### 3.5 NG=8 / small=4 / OK=4

已确证的部分：

- 这三个参数属于 **AF 算法单例**，通过虚函数暴露给调试打印，不是普通全局变量：
  - 单例 getter `0x2c01bc` → 返回 `.data` 里的静态对象基址 `0x81561cf4`（`0x2c01d0: ldr r0,[pc,#0x3c]`，池 `0x2c0214`）
  - 构造：`0x2c01fc bl 0x80507d7c(0x81561cf8, 0x802bff08)`；`0x802bff08` 是构造壳
    （`ldr r3,[pc,#4]; str r3,[r0]; bx lr` → 装载 vtable `0x805de008`）
  - **vtable = `.data` 地址 `0x805de008`**（rel `0x5de008`），其中 `+0x1e8 … +0x264` 是 30 多个取值访问器
    （`0x2c1020 … 0x2c16b4`），调试状态打印 `0x2fa3a4` 通过 `[vtbl+0x1e8] / +0x1f8 / +0x1fc / +0x204 / +0x208 / +0x20c / +0x224 / +0x230 / +0x25c / +0x240 / +0x244` 逐个取值。
- 调试命令行 `af multi algo tracking ng small ok` 与三行说明文字在 `0x2fa384 / 0x2fa388`；
  其中 `0x743a7d "# of succesive ngs to stop tracking: %d"`、`0x743abc "# of oks to start next tracking: %d"` 与
  `0x743a9c "# of small object chatterings: %d"` 说明三个参数确实是"连续 NG 次数上限 / 连续 OK 次数阈值 / 小目标抖动次数"。
- 追踪启停状态机（候选，**未证实**与 NG/OK 的直接连线）：
  - `0x34e4a8`，状态字节 `GAF[0x20]`，取值 `{0,1,3,4,5,7,8}`
  - `0x34e4bc`：`cmp r3,#8` → 走 `0x34e518`（无操作）
  - `0x34e4c0/0x34e4c8`：状态 0/1 → 置 3（`0x34e510`）
  - `0x34e4d0`：状态 3 → 置 4（`0x34e520`）
  - `0x34e4d8`：状态 4 → 置 5（`0x34e4dc`，`strbeq`）
  - `0x34e4e8`：状态 5 且 `GAF[0xf] & 0x0a == 0` → 置 7（`0x34e504`）并清 `GAF[0xac0]`
  - `0x34e52c-0x34e548`：打印 `"[GROUP] MULTI START TRACKING"`（字符串 `0x74e9ac`，池 `0x34e554`）
  - 语义：**状态 5→7 就是"开始跟踪"的那一步**，但触发它的条件是 `GAF[0xf]` 的两位标志，而不是直接读 OK 计数。
  - `0x34e4f0/0x34e4f4` 的 `tst r2,#0x0a` 说明还有另一路（`0x0xa` = bit1|bit3）能阻止迁移。

**未定位（明确写"未知"）：**

- `NG=8` / `small=4` / `OK=4` 三个**初值的写入地址**，本次未找到。
  已排除的路径（都逐条反汇编确认不是）：
  1. `mov #8` + `str` 紧跟 `mov #4` + `str` 的同基址连写模式 —— 全镜像仅 3 处命中（`0x4fb9c`、`0x42dfec`、`0x42e00c`），均与本议题无关；
  2. `mov #8` + `strb` + `mov #4` + `strb` —— 全镜像仅 1 处（`0x103b74`，是枚举 setter，写 `[r4,#0x260]`）；
  3. 连续常量 `8,4,4`（字）—— 仅 `0x5e57f8`，是图像缩放/核系数表；连续字节 `08 04 04` 命中均在字符串区，属文本巧合；
  4. `str r1/r2/r3` 三连参数 setter 模式 —— 61 处全部检查，AF 区内没有形如 `SetTrackingParams(this,ng,small,ok)` 的函数；
  5. 从调试表 `0x2fa2c0` 反查引用：**没有任何 `movw/movt`、常量池字、`adr` 指向该表**，说明该表由更外层（可能跨模块的字符串索引）使用，本镜像内没有直接寻址点。

- 下一步的最短路径（建议）：
  1. 在 `0x81561cf4` 单例的构造路径（`0x80507d7c` 调用的 `0x802bff08` 壳之后的真正构造函数）上打断点/找 `memset`+字段初始化，三个参数多半以 `strb #8` / `strb #4` / `strb #4` 形式落在该对象的低偏移处；
  2. 或者从 `0x34e4a8` 的状态 5→7 迁移条件反推：找到置 `GAF[0xf]` bit1/bit3 的位置，那里必然做"连续 OK 计数 ≥ 阈值"的判断；
  3. 调试命令行 handler 未在本镜像中以字符串直接寻址，可能由 `CCaptureDebugParser` 之类的表驱动解析器按 *词法匹配* 分派（`0x2fa2c0` 附近的表是"命令名 + 帮助串"对），需从 `0x2f92f4` 的调用者集合里找参数个数为 3 的那一支。

---

## 四、风险汇总与未解项

| 项 | 结论 | 风险 |
|---|---|---|
| 算法号 `0x33bff0` 1→0 | 单指令可改，数据/布局均无冲突 | 中：二阶对评价距离噪声敏感，可能过冲；头 2 帧仍退化 |
| `0x33c044` (Conti-N) 同类硬编码 `0x33c068` | 存在，但走 `0x3706c4` 另一套实现 | 未评估，需单独做 |
| 800ms 拆分 `0x2f6dd4` | 单指令 `cmp r8,r2,lsl #3` 即 100ms，stage2 语义不变 | 低-中：框更易闪烁；建议先用 200ms |
| `0x2f6c48` 调用者 | 仅 `0x2f7730` 一处 | 低 |
| 主脸打分加惩罚项 | 需 2 指令 + 1 个 4 字节洞；不必新增全局 | 中：1 帧身份粘性 |
| NG/OK/small 初值 | **未知**（已排除 5 条检索路径） | 阻断"直接改阈值"这一杠杆 |
| `0x349ca8` 的真实身份 | 事件 `0x306` 的一个 case，非算法分派器 | 旧结论需修正 |
| GAF 基址 | `0x81550408`（`0x816d429c` 是 trace ctx） | 旧结论需修正，相关字段偏移全部复核 |
