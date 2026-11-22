# AE → AF 参数耦合静态逆向（v1.12 / p7）

- 日期：2026-11-17
- 镜像：`E:/NXWA/fw/v1.12/p7_wrapped.elf`
- 口径：`chunk = runtime - 0x80000000`，`ELF 文件偏移 = chunk + 0x1000`
- 方法：纯静态。capstone `CS_MODE_ARM`，按 `push {...,lr}` 建函数起点表；调用图用 raw `BL`(0xEB) 全段扫描；字段归属用寄存器级轻量污点，避免"不同对象同偏移"误判。
- 局限：镜像只覆盖 `0x80000000..0x80B84B80`（代码段）。所有 `0x81xxxxxx` 的 DDR 全局（AE 上下文、GAF）**不落在文件里**，只能给地址、不能给初值。

---

## 0. 结论摘要

1. `AeCalcAFParamContiConti`（`0x802D8ED8`）**不是**一个大计算函数，而是 `0x70` 字节的协调器：它把 3 个子函数串起来，全部结果写进 **AE 上下文单例 `0x8156EAF8`**，不直接写 AF 模块、也不直接调 IQ `SetParameter`。
2. 它的输入**只有亮度统计**：liveview 人脸平均 Y + 221 bin 加权直方图 + 8 区亮度和；输出是 **1/16 EV 单位的亮度偏移** 和 **EV/LV 当前值**。
3. 唯一确定的 AE→AF 正式接口是：AE 的 **LV（`eCALC_LV_CURR`）与 ISO**，被 AF 的 `UpdatetHpfLpfFrequencyCoring`（`0x803351A0`）通过 `0x802C8908(ae, idx)` 读走，写进 AF 参数 `0x20002 / 0x20004 / 0x20007 / 0x2000C`，再由 `SetAfFilterToDsp`（`0x803353D4`）落到 DSP 的 AF 滤波器。字符串 `0x8074AB2C`「UPDATE HPF-CORING(...)(ISO:%d)」与 `0x8074ABA0`「UpdateAFRegister::ISO[%d]」是直接证据。
4. `0x8034BEBC` 的真实身份是 **`saveAfState` —— AF 状态轨迹记录器**（环形缓冲 + 互斥 + "[GROUP] AF STATE" 日志），**不是** AF 执行阶段的状态切换器。`mode=1/2` 只影响被记录的值，不影响控制流。
5. 暗光最可能被拖慢的一环：**ISO 抬升 → AF HPF/LPF coring 参数整体重算 → 对比度评价函数峰展宽/信噪比下降**（`0x803351A0`→`0x803353D4`，含专用「ISO6400」分支）。次要但确凿：**GAF+0x5c 进入 SLOW-AF 后，FD 区域准入门强制 800 ms 延迟**（`0x802F6C48`）。

---

## 1. `0x802D8ED8` 全解

### 1.1 边界与栈帧

| 项 | 值 |
|---|---|
| 起点 | `0x802D8ED8` |
| 终点 | `0x802D8F48`（`pop {r4,pc}`），下界 `0x802D8F4C` 属下一个函数 |
| 长度 | `0x70` |
| 序言 | `push {r4,lr}` + `sub sp,#0x10` → 栈帧 `0x18` |
| 返回 | `void`（尾部走日志调用后 `pop {r4,pc}`） |

### 1.2 主体（完整）

```asm
802D8ED8  push    {r4, lr}
802D8EDC  sub     sp, sp, #0x10
802D8EE0  mov     r4, r0                  ; r4 = AE 上下文
802D8EE4  bl      #0x802D4ED0             ; = AeObj (0x8156EA84)
802D8EE8  bl      #0x802D5180             ; = 统计块指针 AeObj->[0xc]
802D8EEC  mov     r1, r0
802D8EF0  mov     r0, r4
802D8EF4  bl      #0x802D8E18             ; sub1(ctx, statblock)
802D8EF8  bl      #0x802D4ED0
802D8EFC  bl      #0x802D5180
802D8F00  mov     r1, r0
802D8F04  mov     r0, r4
802D8F08  bl      #0x802D8068             ; sub2(ctx, statblock)
802D8F0C  mov     r0, r4
802D8F10  bl      #0x802D9B7C             ; sub3(ctx)
802D8F14  mov     r2, #5
802D8F18  ldrsh   r3, [r4, #0xaa]        ; 日志参数 = ctx->0xaa（镜头修正量）
802D8F1C  str     r2, [sp]
802D8F20  movw    r2, #0x1f80
802D8F24  movt    r2, #0x8074             ; r2 = 0x80741F80
802D8F28  movw    r0, #0x65c0
802D8F2C  stmib   sp, {r2, r3}
802D8F30  movt    r0, #0x806f             ; r0 = 0x806F65C0（模块 TAG）
802D8F34  ldr     r1, [pc, #0x10]        ; = 0x805E1DBC "AeCalcAFParamContiConti"
802D8F38  mov     r2, #0x340              ; 行号 0x340
802D8F3C  mov     r3, #0
802D8F40  bl      #0x80046484             ; logutil_module_save
802D8F44  add     sp, sp, #0x10
802D8F48  pop     {r4, pc}
```

- **入参**：`r0` = AE 上下文 `0x8156EAF8`（由 `0x802D6EFC()` 返回，C++ 函数内静态单例 + 守卫）。`r1/r2/r3` 未使用。
- **返回**：无（`void`）。
- 日志格式串在 `0x80741F80`：`"AeCalcAFParamContiConti(%x)"`，实参 = `ctx+0xaa`。

### 1.3 三个子函数做了什么

`0x802D4ED0()` 返回 **AeObj = `0x8156EA84`**；`0x802D5180(AeObj)` 返回 **统计块指针**（`AeObj->[0xc]`，未分配时先调 `StatisticsAlloc`）。

#### sub1 `0x802D8E18(ctx, stat)` —— 三路亮度 → 1/16 EV 偏移

```asm
802D8E20  ldrh    ip, [r1, #0x772]     ; b
802D8E28  ldrh    r2, [r1, #0x770]     ; a
802D8E30  ldrh    r3, [r1, #0x774]     ; c
802D8E6C  bl      #0x802D8DB4          ; face = 人脸亮度(Q8 EV)，无脸 → 0
802D8E7C  bl      #0x802E3890          ; hist = 直方图亮度(Q8 EV)，ref=0x2a8
802D8E90  bl      #0x802E26F4          ; ctx[0xa4] = f((face+8)>>4, {a,b,c})
802D8EA8  bl      #0x802E26F4          ; ctx[0xa6] = f((hist+8)>>4, {a,b,c})
802D8EB8  bl      #0x802E26F4          ; ctx[0xa8] = f(0,           {a,b,c})
802D8EC0  bl      #0x802CE164
802D8ECC  strh    r2, [r0, r3]         ; AeCtx2[0x5da] = ctx[0xa8]
```

`0x802E26F4(x, p)` 的算术（`p` = 栈上 3 个 s16）：

```
x = clamp(x, -0x9F, +0x50)          ; -159 .. +80
return x + p[0] + p[1] + (0x50 - p[2])
```

即 `80 - c` 是"以 5 EV 为基准的补量"，整体是 **1/16 EV 单位的偏移加法**。

`0x802E3890(stat, ref)` = 加权平均亮度：

```
total = acc = 0
for i in 0..0xdc (221 bins):           ; 权重表 0x805E3FA0（运行期填充，静态镜像为 0）
    w = tbl[i];  if w: total += w; acc += stat_u16[i] * w
avg = (acc / max(total,1)) >> 2
v   = LUT(avg)                          ; 0x802E26AC → 表 @0x8108F4F0[clamp(0..0x3FF)]
return (v - ref) * 256 / 100            ; ref=0x2a8(680) → Q8 定点
```

`0x802E3860(faceStruct, ref)` 同构，只是取 `faceStruct[0xc]` 一个值。
`0x802D8DB4()` 先把 `AeCtx2[0x5b2] = -1` 作哨兵，调 `AeGetStatisticsLiveviewFaceY` 填充 `AeCtx2+0x5a6`（`0x5a6+0xc == 0x5b2`），若仍为 `-1`（无脸）则返回 0。

> 单位推断：`(v - 680) * 256 / 100` 只在 `v` 是 **1/100 EV** 时量纲自洽，结果即 **Q8 EV**；再 `(x+8)>>4` 得 **1/16 EV**。LUT 本体在 `0x8108F4F0`（DDR，不在镜像内），**无法直接证明是 log2 表**，此处为量纲反推。

#### sub2 `0x802D8068(ctx, stat)` —— EV / LV 当前值

```asm
802D8094  mov     r1, #0x2bc
802D80C4  bl      #0x802E3584          ; 10 个 s16 区亮度求和(stat+0x94,96,98,9a,9c, b6,b8,ba,bc,be)
                                     ;   → LUT → (v-700)*256/100
802D80CC  add     ip, r0, #8
802D80D0  asr     ip, ip, #4           ; Q8 → 1/16 EV
802D80E8  movge   r0, ip               ; 下限 -0x40
802D80F0  bl      #0x802E2728          ; 含 r2/r3 附加项
802D80F8  strh    r0, [r5, #0xae]      ; ctx[0xae] = LV
802D8100  bl      #0x802E26F4
802D8104  strh    r0, [r5, #0xac]      ; ctx[0xac] = EV
```

#### sub3 `0x802D9B7C(ctx)` —— 镜头修正 + AE 参数块下发 + 亮暗模式字节

```asm
802D9B94  bl      #0x802D6FB8          ; 读 AeCtx2+0x204+1*0x20 的 32B 块到栈
802D9B98  bl      #0x802F1EB0
802D9B9C  bl      #0x802F3BF0
802D9BA4  bl      #0x802D7954          ; 条件成立时：ctx[0xa4] = ctx[0xa6]（锁存）
802D9BBC  ldrh    r3, [r4, #0xaa]
802D9BC0  rsb     r0, r0, r3            ; ctx[0xaa] -= 镜头位置
802D9BC4  strh    r0, [r4, #0xaa]
802D9BE4  bl      #0x802D6F74          ; AeCtx2[0x204 + 3*0x20]  <- 32B 块
802D9BF4  bl      #0x802D6F74          ; AeCtx2[0x204 + 0xb*0x20] <- 同块
802D9BF8  ldrsh   r3, [sp]             ; 亮度判据
802D9BFC  cmp     r3, #0x3e            ; <= 62  → 3
802D9C08  cmp     r3, #0x6e            ; <=110  → 0 ；>110 → 1
802D9C14  bl      #0x802CB4F4
802D9C18  strb    r4, [r0, #0x14]      ; byte[0x8156C514] = {0,1,3}
```

---

## 2. 它写了哪些 AF 参数

写入目标全部是 **AE 侧全局**，地址可精确给出：

| 目标地址 | 宽度 | 字段 | 来源 |
|---|---|---|---|
| `0x8156EB9C` | u16 | ctx+0xa4 | sub1：人脸 Y 派生的 1/16 EV；随后可能被 sub3 用 +0xa6 锁存覆盖 |
| `0x8156EB9E` | u16 | ctx+0xa6 | sub1：221-bin 直方图派生的 1/16 EV |
| `0x8156EBA0` | u16 | ctx+0xa8 | sub1：仅含 `a+b+(80-c)` 基值 |
| `0x8156EBA2` | u16 | ctx+0xaa | sub3：镜头位置修正量（也是日志 `(%x)` 的实参） |
| `0x8156EBA4` | u16 | ctx+0xac | sub2：**`eCALC_EV_CURR`** |
| `0x8156EBA6` | u16 | ctx+0xae | sub2：**`eCALC_LV_CURR`** |
| `0x8156CDE2` | u16 | AeCtx2+0x5da | sub1：`ctx+0xa8` 的副本 |
| `0x8156CA6C` | 32 B | AeCtx2+0x204+3*0x20 | sub3：AE 参数块 idx=3 |
| `0x8156CB6C` | 32 B | AeCtx2+0x204+11*0x20 | sub3：AE 参数块 idx=11 |
| `0x8156C514` | u8 | AeState+0x14 | sub3：亮度阈值 62/110 → `{3, 0, 1}` |

（`0x8156EAF8` = AE 上下文；`0x8156C808` = AeCtx2；`0x8156C500` = AeState）

### 2.1 `+0xac` / `+0xae` 的 id 解码（可证）

`0x802D1914(AeCtx2, id)` 是 AE 参数取值器，`sub r3,id,#1; cmp #0x12; ldrls pc,[pc,r3,lsl#2]`，跳表 @`0x802D1934`：

| id | 跳转 | 读取偏移 |
|---|---|---|
| 9 | `0x802D1A48` | `[obj,#0xac]` |
| 11 | `0x802D1A60` | `[obj,#0xae]` |

配套 trace 函数 `0x802D178C` 的字符串表按 `0x1C` 步长排列（`0x807412B8` 起）：
`0x80741398 = ".eCALC_EV_CURR"`（第 9 项）、`0x807413D0 = ".eCALC_LV_CURR"`（第 11 项）。
两表 id 顺序一致 → **`+0xac = eCALC_EV_CURR`，`+0xae = eCALC_LV_CURR`**。

> 因此 `AeCalcAFParamContiConti` 对外真正"可被别的模块按 id 取到"的产物，就是 **EV/LV 两个当前值**。`+0xa4/+0xa6/+0xa8/+0xaa` 不在该 id 表里。

### 2.2 是否写 IQ 单例 / CAF hub

- IQ 单例 `0x81561CF4`：全镜像只有 2 处字面量引用，都在 `0x802C01BC`（IQ 访问器）。`AeCalcAFParamContiConti` 及其 3 个子函数**没有任何调用链到达它**。
- CAFData hub `0x816D2CE0`：只有 2 处引用，在 `0x80336E90` 附近（`getFp2Puls` 区域），与本函数无关。
- **结论：本函数不写 IQ 单例、不写 CAFData hub。**

---

## 3. 谁调用它

调用链（逐层，全部有 BL 站点）：

```
0x800A9854  (3A 事件分发器, 3A 任务)
  └─ BL 0x8009A904 @0x800A9A3C   (r1 = r1 & 1 传入)
       0x8009A904  适配 thunk：  bl 0x802AEBD8 → mov r1,r4 → pop → b 0x802AFB34
         └─ IQ_AE_ContiContiAe  @0x802AFB34
              └─ BL 0x802CA380 @0x802AFB80
                   ContiContiAe @0x802CA380
                     ├─ BL 0x802D887C @0x802CA438   AeCalcCaptParamContiConti
                     └─ BL 0x802D8ED8 @0x802CA56C   AeCalcAFParamContiConti   ★
```

另一路调用者 `0x80184FC8`（func `0x80184E30`）也到达同一个 thunk。

### 生效条件（`ContiContiAe` 内）

```asm
802CA420  cmp     r6, #0            ; arg2 != 0 → 走拍照分支，跳过
802CA424  bne     #0x802CA550
802CA428  cmp     r7, #0            ; r7 = byte[AeObj+0x78c] = flash 标志
802CA42C  bne     #0x802CA518       ; 闪光时跳过
802CA43C  ...                       ; sy_438 == 4 时整段跳过参数拷贝
802CA4B0  cmp     r5, #0            ; r5 = (sy_43c == 2)
802CA4B4  beq     #0x802CA568       ; ★ 仅当 sy_43c != 2 才调用
802CA568  bl      #0x802D6EFC
802CA56C  bl      #0x802D8ED8       ; AeCalcAFParamContiConti
```

- `sy_43c` = `AeCtx2+0x43c` 的 s16 **AE 状态字段**（值域观测到 1..4 被比较）。**它 == 2 时不执行**。该字段语义未定，**不在** `0x802D1914` 的 id 表内（id 表只覆盖 `+0xa4..+0xde`），因此不能等同于 `eCALC_LV_S1`。
- 另有前置闸：`ldrsh r2,[AeCtx2,#0x432]; ldr r3,[ctx,#0x2c]; if (r2 > r3) → "ContiContiAe NoCalc by EVC(%d)"` 直接返回（EV 补偿超限时不算）。

### 频率判定

驱动源是 3A 任务的事件循环（`0x800A9854`），不是 AF 请求。同一条链路里还有 `HandleEventVDMain`（`0x802C9370`）、`AeGetStatistcsCapture`（`0x802D617C`）——都是每帧 VD 统计就绪后跑一次。

**判定：每个连续测光 tick（≈ 每帧 liveview）执行一次；不是每次 AF 启动，也不是只在 AE 收敛后。**

---

## 4. `0x8034BEBC`（`saveAfState`）做什么

**不是状态切换器，是 AF 状态轨迹记录器。**

```asm
8034BED0  bl      #0x804DD064        ; 毫秒 tick（get_tick / 1000）
8034BED4  cmp     r0, #0x3e8         ; < 1000ms → 直接返回
8034BED8  blt     #0x8034BF80
8034BEDC  ldr     r8, [pc,#0xf8]     ; = 0x81550408 (GAF)
8034BEE0  ldrb    r3, [r8, #0x5c]    ; ★ AF 分组模式
8034BEE4  cmp     r3, #1
8034BEE8  beq     #0x8034BF80        ; ★ 0x5c==1(SLOW-AF) → 直接返回，不记录
8034BEFC  mov     r0, r7             ; this+0x114
8034BF00  bl      #0x8034B6F0        ; 取互斥(this->[0x1c])  timeout=-1
8034BF04  bl      #0x804DD064        ; 再取一次 ms
8034BF30  add     ip, r5, r6, lsl #16
8034BF3C  str     ip, [r4, #0x140]   ; 打包记录：r1 | (r2<<16)
8034BF28  strh    sl, [r4, r0]       ; 类型 = 0xc
8034BF50  bl      #0x80509EEC        ; 追加进环形缓冲
8034BF68  bl      #0x8034B700        ; 放锁
8034BF6C  ldrb    r3, [r8, #0x12]
8034BF70  and     r3, r3, #8
8034BF7C  bne     #0x8034BFB4        ; 置位则打印
8034BFBC  ... = 0x8074E80C "[GROUP] AF STATE (%d)(%04X)"
```

- 参数：`r0` 恒为固定全局 `0x816D429C`（日志对象），`r1`、`r2` 是被记录的两个值。
- `mode` 差异：

| 调用者 | 前置写 | 传入 r1,r2 | 实际结果 |
|---|---|---|---|
| `onStartSlowAF` `0x802AE128` | `GAF+0x5c = 1` | `2, 0` | **被 `0x5c==1` 守卫吞掉，不记录** |
| `onStartS0AF` `0x802AE19C` | `GAF+0x5c = 2`，另 `GAF+0xac4 = 3` | `1, 0` | 正常记录 `0x0C/0x02` 一条 |

- 关键副作用：**SLOW-AF 分支在写 `GAF+0x5c=1` 之后才调用它，所以 SLOW-AF 切换永远不进轨迹**。这也是"暗光下 AF 日志更少"的直接机制。
- 其余 13 个调用点分布在 `0x802AC8B4`(S2 按键序列)、`0x802AD098`、`0x802AD38C`(sendAfNotifyResult)、`0x802AE07C`、`0x802AE218`(onStartS0)、`0x802AE310`(onStartS1)、`0x802F5B48`、`0x802F5C54` —— 全部是各状态迁移点的埋点，进一步确证它是**轨迹记录器**。

---

## 5. AE → AF 参数链

### 5.1 文本图

```
[ISP 统计]  Y/直方图/8 区
      │
      ▼
AeObj(0x8156EA84)->[0xc]  统计块
      │
      ├─► 0x802D8DB4  人脸平均 Y ──► LUT(0x802E26AC) ─► (v-680)*256/100 ─┐
      ├─► 0x802E3890  221bin 加权 ──► LUT              ─► (v-680)*256/100 ─┤
      └─► 0x802E3584  10 项区亮度和 ──► LUT             ─► (v-700)*256/100 ─┤
                                                                          │
   AeCalcAFParamContiConti(0x802D8ED8)   ◄──────────────────────────────┘
      │  每连续测光 tick，AeCtx2+0x43c != 2 且非闪光
      ├─ ctx[0xa4/a6/a8]  = 1/16 EV 亮度偏移   (0x8156EB9C/9E/A0)
      ├─ ctx[0xaa]        = 镜头位置修正        (0x8156EBA2)
      ├─ ctx[0xac]        = eCALC_EV_CURR       (0x8156EBA4)
      ├─ ctx[0xae]        = eCALC_LV_CURR       (0x8156EBA6)  ★
      ├─ AeCtx2[0x5da]    = ctx[0xa8]           (0x8156CDE2)
      ├─ AE 参数块 idx=3 / idx=11               (0x8156CA6C / 0x8156CB6C)
      └─ AeState+0x14     = {3,0,1}             (0x8156C514)
                    │
                    │  AF 按 idx 取值：0x802C8908(ae, idx) → 0x802D1D00 → 0x802D1914(id)
                    ▼
      UpdatetHpfLpfFrequencyCoring (0x803351A0)      ← 仅当 AF 参数 0x40007 == 1
         idx 0x41 → eCALC_LV_CURR  (0x803352D4)
         idx 0x00 → ISO/SV 路径    (0x803352B4)
         idx 0x14 → 附加量         (0x803352C4)
                    │
                    ├─ SetParam 0x20002 = LV
                    ├─ SetParam 0x20004 = ISO
                    ├─ SetParam 0x20006 / 0x20007 / 0x2000C（coring 系数）
                    ▼
      SetAfFilterToDsp (0x803353D4)
         GetParam 0x20007 → ISO，>0x64(100)，钳到 0x1900(6400)
         GetParam 0x40011 → reliability
         → "CORING(ISO6400)" / "CORING(NORMAL)" 两套表
                    ▼
              DSP AF 滤波器（HPF/LPF + coring）
```

### 5.2 旁路：模式闸门

```
LCD/按键/亮度 ──► GAF+0x5c  ∈ {1=SLOW-AF, 2=S0-AF}
                    │
      ┌─────────────┴──────────────┐
      ▼                            ▼
0x802F6C48 FD 区域准入门      0x8034BEBC saveAfState
r8 = (0x5c!=2) ? 800 : 0      0x5c==1 → 不记录
→ SLOW-AF 下 FD 等 800ms
```

---

## 6. 关键反汇编（带地址）

### 6.1 AE 参数 id 表（证明 +0xac/+0xae 语义）

```
0x802D1914  sub r3,r4,#1 ; cmp #0x12 ; ldrls pc,[pc,r3,lsl#2]   ; 跳表 @0x802D1934
  table[8]  → 0x802D1A48   ldrsh r2,[r0,#0xac]
  table[10] → 0x802D1A60   ldrsh r2,[r0,#0xae]
0x80741398  ".eCALC_EV_CURR"     0x807413D0  ".eCALC_LV_CURR"
```

### 6.2 AF 侧读取 AE 值

```
803352A8  bl      #0x802C8638            ; AE 对象
803352B4  bl      #0x802C8908            ; idx 0
803352B8  mov     r1, #0x14
803352C4  bl      #0x802C8908            ; idx 0x14
803352C8  mov     r1, #0x41              ; ★ eCALC_LV_CURR
803352D4  bl      #0x802C8908
803352E8  bl      #0x8033273C            ; SetParam(this, 0x20002, LV)
803352FC  bl      #0x8033273C            ; SetParam(this, 0x20004, ISO)
```

### 6.3 ISO 决定 coring（字符串 + 钳位）

```
0x803351F8  ldr r1,=0x8074AB2C  "--- UPDATE HPF-CORING(%s)(RELIABILITY:%d)(ISO:%d)(KNEE: %d)"
0x803353D4  SetAfFilterToDsp:
  0x80335414  bl 0x80332948   ; GetParam(this, 0x20007) → ISO
  0x80335448  cmp r4,#0x64    ; >100
  0x80335450  cmp r4,#0x1900  ; 钳到 6400
0x8074ABA0  " UpdateAFRegister::ISO[%d]"
0x807561CC  "CORING(ISO6400):(ISO:%d)(KNEE:%d)(YMAX: %d)(CORING THRESHOLD: %d)"
0x8075622C  "CORING(NORMAL):(ISO:%d)(KNEE:%d)(YMAX: %d)(CORING THRESHOLD: %d)"
```

### 6.4 模式闸门

```
802F6C7C  ldrb r3,[r4,#4]
802F6C80  ldrb r2,[r4,#0x5c]           ; GAF+0x5c
802F6C88  cmp  r2,#2
802F6C8C  movne r8,#0x320              ; 800 ms
802F6C90  moveq r8,r5                  ; 0
802F6CF0  bl   0x804DD064              ; ms
802F6CFC  cmp  r8,r2                   ; elapsed
802F6D00  bgt  0x802F6E0C              ; 不够就等

8034BEE0  ldrb r3,[r8,#0x5c]           ; saveAfState
8034BEE8  beq 0x8034BF80               ; ==1 → 直接 return
```

### 6.5 微扫描准入也吃 LV/ISO

```
0x8074C5D8  "MICRO SCAN CHECK --> NO CONDITION(#4) AV_AF:0x%x,AV_CAP:0x%x,H-FREQ:%d, LV:0x%02X, ISO:0x%x"
调用点：canStartMicroScan (0x8033F058)
  8033F088  ldrb r3,[r4,#0x5c]
  8033F090  cmp  r3,#2
  8033F094  bhi  0x8033F0E4            ; 0x5c<=2 → 直接判 NO CONDITION
```

---

## 7. 结论：AE 到底通过哪几个参数影响 AF

按证据强度排序（只列 3 条）：

### #1　LV（`eCALC_LV_CURR`, `0x8156EBA6`）→ AF HPF/LPF 频率 + coring
- 证据：`0x803352D0~0x803352E8`（`mov r1,#0x41` → `bl 0x802C8908` → `SetParam 0x20002`）；id 表 `0x802D1914` + 字符串 `0x807413D0` 证明 `+0xae` 就是 LV；`0x802D8ED8` 每 tick 重算它。
- 暗光行为：LV 下降 → AF 滤波参数整体下移 → 评价函数更平、峰更宽。

### #2　ISO → coring 阈值 / 截止频率（最强实测相关）
- 证据：`0x803351A0` 的 `ISO` 分支 + 字符串 `0x8074AB2C`、`0x8074ABA0`、`0x807561CC`、`0x8075622C`；`0x80335450` 的 `cmp #0x1900`(6400) 钳位说明存在专门的高 ISO 表。
- 暗光行为：ISO 升高 → coring 阈值提高、HPF 截止降低 → 对比度峰值钝化，需要更多扫描步/重确认。

### #3　`GAF+0x5c` 分组模式 → SLOW-AF 通路
- 证据：`0x802F6C88` 起 `800ms vs 0ms`；`0x8034BEE0` 起 `0x5c==1` 抑制轨迹。
- 暗光行为：进入 SLOW-AF 后，FD 区域准入多等 800 ms；且该状态下 AF 状态**不再留日志**，排障时会误以为"没动"。

> 第 3 条不是"参数"而是"模式"，但它对体感延迟的贡献是量级最大的（800 ms/次）。

---

## 8. 上机验证：读哪个量最直接

按"信息量 / 读取难度"排序（`/dev/mem` 直接读，全部是小端 u8/u16）：

| 优先级 | 地址 | 类型 | 含义 | 判读 |
|---|---|---|---|---|
| ★1 | `0x81550464` | u8 | `GAF+0x5c` AF 分组模式 | 值 1 = SLOW-AF（800 ms FD 闸 + 无轨迹）；值 2 = S0-AF（0 ms）。**一个字节判定是否掉进慢速通路** |
| ★2 | `0x8156EBA6` | u16（有符号） | `eCALC_LV_CURR` | 暗光下降；与 AF 参数 `0x20002` 同源 |
| ★3 | `0x8156EBA4` | u16 | `eCALC_EV_CURR` | 与 LV 一起看，可判断是"亮度低"还是"曝光策略偏" |
| ★4 | `0x815504A8` | u32 | `GAF+0xa0` 上次 FD 闸通过时刻(ms) | 配合当次 tick，直接算出 FD 闸是否在耗 800 ms |
| ★5 | `0x81550ECC` | u32 | `GAF+0xac4` | `onStartS0AF` 写 3，可作模式切换计数 |

建议采法：暗光/亮光各录一段 `1 Hz` 采样，同时记 `0x81550464` 与 `0x8156EBA6`。
若 `0x5c` 长时间停在 `1`，且 `tick - [0x815504A8]` 逼近 800，则"慢"主要来自模式闸门而非算法本身。

---

## 9. 未解项（不用"很可能"包装）

1. **LUT `0x8108F4F0`（1024×u32）不在切片镜像内**（`0x8108F4F0 > 0x80B84B80`），无法直接证明它是 log2/EV 表；当前"Q8/Q4 EV"结论完全来自 `(v-680)*256/100` 与 `(x+8)>>4` 的量纲反推。若要证实，需从 `NX500_v1.12.bin` 全镜像按 DDR 映射取该段。
2. **`stat+0x770 / +0x772 / +0x774` 三个 s16 的语义未定**。已知它们以 `x + a + b + (80 - c)` 的形式进入结果，且 `0x50` 对应 5 EV，推测是曝光补偿的拆分项（主/微/闪光），但未找到写入侧。同类的 `AeCtx2+0x432 / +0x438 / +0x43c` 三个 s16 状态字也未定名（`+0x432` 有旁证：日志串 `"ContiContiAe NoCalc by EVC(%d)"` 打印的正是它；`+0x43c` 只观测到与 `2` 比较，语义未知）。
3. **`ctx+0xa4 / +0xa6 / +0xa8 / +0xaa` 在 AE 模块内没有任何静态读者**。已确认：AE 上下文 `0x8156EAF8` 全镜像只有 2 处字面量引用（均在 `0x802D6EFC` 访问器内），所有消费必须经过该访问器；对 34 个调用者做寄存器级污点后，只命中 `+0xac`、`+0xae`、`+0xb6`、`+0xba`、`+0xa4`(写)。因此这 4 个字段要么只经 `AeCtx2+0x5da` / AE 参数块 idx=3,11 间接下发到 DSP，要么是给未包含在本镜像中的其他核使用——**两种都未证实**。
4. **`0x802D5D1C(ctx)` 的条件未解**：它决定 sub3 是否用 `+0xa6` 覆盖 `+0xa4`（即"瞬时值是否锁存为保持值"）。这是 AF 亮度输入是否被平滑的关键，值得单独一轮。
5. **`idx 0` = ISO 只是反推**：证据是 `0x803352B4` 处调用点对应的日志标签为 ISO、以及 `SetAfFilterToDsp` 在 6400 处钳位；但 `0x802CE88C → 0x802CE600(case 0)` 的实体只是"用 `AeCtx2[0x8d6]/[0x8ca]` 做上下限钳位"，其输入来自 `AeCtx2+0x244` 的 8 字结构。该结构由谁填充未定位。
6. **`0x802D6F74/0x802D6FB8` 的 32 字节参数块 idx=3、idx=11 的字段布局未解**（只知道是 8 个 u32，从 idx=1 拷贝而来）。
7. 反汇编注意：以下位置是**字面量池**，被 capstone 误当指令，勿采信 —— `0x802D8F4C`、`0x802D1D14`(79 项跳表)、`0x802D1934`(19 项跳表)、`0x802D17A8`(18 项跳表)、`0x802D18F0~0x802D1910`(字符串指针表)、`0x8009A8A0` 附近的 thunk 尾部。
