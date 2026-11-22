# NX500 v1.12 —— SLOW-AF 触发链与暗光判据 静态分析报告

- 日期：2026-11-17
- 镜像：`E:/NXWA/fw/v1.12/p7_wrapped.elf`（单一 PT_LOAD：file 0x1000 → vaddr 0x80000000，size 0xB84280，即镜像覆盖 0x80000000–0x80B84280，其余地址按 RAM/BSS 处理）
- 口径：`chunk = runtime - 0x80000000`，`file_off = chunk + 0x1000`
- 工具：capstone 5 ARM/ARM 模式；全镜像 BL/B/BLX 扫描 + 字面量池 + movw/movt + 4 字节裸指针扫描
- 只做静态分析，未接相机

---

## 结论摘要

| # | 结论 | 置信度 |
|---|---|---|
| 1 | SLOW-AF 是 **CAF（连续 AF）模块内部的一个"慢速扫描"子模式**（`CAF::SlowAFStart` / `onStartSlowAF`），不是独立的低照度功能 | 高 |
| 2 | 它的整条链上 **没有读任何照度量**（ISO / BV / LV / Tv / 主脸 Y 都没有），判据只有 AF 内部状态位 + CAF 状态 + 镜头通信结果 | 高 |
| 3 | 在本镜像里 SLOW-AF **没有可达的自动触发点**：唯一业务入口 `CIQIfAf::Start(2)` 的调用者 `0x800ABC8C` 全镜像零引用；另一入口是 AF 命令表的 `slafsta`（其宿主函数 0x802FB474 在本镜像内也无静态引用） | 高（负证据：三层扫描均为 0 命中） |
| 4 | 同族的 **S0-AF 才是活的**：入口 `0x800ABC1C` 挂在 `C3AStateLockCAF` 虚表槽（0x80577064+0x2C），由连续 AF 状态机驱动 | 中高 |
| 5 | `Average Y Data of Main Face` **只被一个调试打印函数消费**，没有进入 AF/AE 决策 | 高 |
| 6 | `PafAdjust.csv` **不是运行时加载的参数文件**，是 PAF 工厂调整工序写出的 CSV 日志名；参数实体在 RAM（0x815555B8 / 0x81555B08），可运行时改写 | 高 |

一句话：**SLOW-AF 由 AF 模块（连续 AF）的状态机/命令触发，不由暗光触发；且 v1.12 里这条路径实际上断了。**

---

## 一、触发调用树

### 1.1 锚点复核（与题给一致，补两点）

`0x802AE128`（`onStartSlowAF`）：

```
802AE12C  movw r3,#0x408 / movt r3,#0x8155   ; r3 = GAF = 0x81550408
802AE148  mov  r4,#1
802AE150  strb r4,[r3,#0x5c]                 ; GAF+0x5c = 1  (SLOW)
802AE140  mov  r1,#2 ; mov r2,#0
802AE154  bl   0x8034BEBC                    ; 投递命令 2（r0=0x816D429C 队列）
802AE15C  bl   0x802ADE84
802AE188  bl   0x80046484                    ; "[GROUP] ### SLOW-AF MODE START NOTIFIED"
```

补充：
- 本函数字面量 `0x805DB110 = "onStartSlowAF"`、`0x805DB0F4 = "onStartS0FromContiNormal"`、`0x805DB120 = "onStartS0AF"` —— 这是一组 AF 状态对象的钩子名。
- `0x802AE19C` 结构完全对称（`strb 2 → GAF+0x5c`，命令 1）。
- `GAF+0x5c` 的取值域（由全部写者归纳）：`0`=空闲、`1`=SLOW-AF、`2`=S0-AF、`3`=普通 AF（由 `0x802AE310` 在 `CAF::AFStart` 里写 3）。
- `GAF+0x5c` 的写者全集（在引用 GAF 的函数内做寄存器污点扫描）只有：`0x802AE128`(1)、`0x802AE19C`(2)、`0x802AE218`(0)、`0x802AE07C`(0)、`0x802AE310`(3)。**写 1 的只有 0x802AE128 一处**，所以下面这条链就是 SLOW 模式的全部来源。

### 1.2 完整调用树（含所有分支形态）

```
[根 A] 0x800ABC8C  (无符号名, 置 this+4=1 后启动)
         └─ b 0x8009B960  CIQIfAf::Start(mode=2)   @0x800ABCBC
              └─ case2 @0x8009BA08
                   └─ bl 0x802AF8D8   (CIQIfAf 薄封装/加锁)
                        └─ bl 0x802F64B8   CAF::SlowAFStart     @0x802AF8EC
                             └─ bl 0x802AE128  onStartSlowAF     @0x802F6548
                                  └─ GAF+0x5c = 1 + 命令2 + 日志

[根 B] 0x802FB474  AF 命令表分发器（argv[1] 逐个 strcmp）
         └─ "slafsta" @0x802FB510 → 0x802FBADC
              └─ bl 0x802F1EB0   CAF::Instance()
              └─ bl 0x802F64B8   CAF::SlowAFStart            @0x802FBAE0
                   └─ bl 0x802AE128                          @0x802F6548
```

两处调用点都落在同一函数 `CAF::SlowAFStart`（0x802F64B8），其内部只有**一个** `bl 0x802AE128`（0x802F6548）。因此 `0x802AE128` 的调用者全集 = `{0x802F6548}`，调用树到此收敛。

### 1.3 根 A 的可达性：断了

`CIQIfAf::Start`（0x8009B960）是一个 `switch(mode)`：

| mode | 目标 | 行为 |
|---|---|---|
| 0 | 0x8009B9E8 → `0x802AF734(this,0)` | 普通 AF 启动（ScanAF 用） |
| 1 | 0x8009B9F8 → `0x802AF734(this,1)` | 普通 AF 启动（LockCAF Restart 用） |
| 2 | 0x8009BA08 → `0x802AF8D8` | **SLOW-AF** |
| 3 | 0x8009B9C0 → `0x802AF840` | S0-AF |

全镜像对 `0x8009B960` 的引用（BL 2 处 + 尾跳 `b` 7 处，共 9 处）：

| 调用点 | 形态 | 传入 mode | 所在函数 |
|---|---|---|---|
| 0x800AABE4 | bl | 1 | 0x800AAB48（`CAP_CTRL / RestartAF`，连续 AF 的 Restart） |
| 0x800ADF20 | bl | 0 | 0x800ADE2C `C3AStateScanAF::startScanAF()` |
| 0x800AB998 | b（尾跳） | 0 | 0x800AB8DC |
| 0x800ABEB8 | b | 0 | 0x800ABE88 |
| 0x800ABC54 | b | **3（S0）** | 0x800ABC1C |
| 0x800ABCBC | b | **2（SLOW）** | **0x800ABC8C** |
| 0x800AC668 | b | 3（S0） | 0x800AC5F0 |
| 0x800ACA24 | b | 3（S0） | 0x800AC97C |
| 0x800BD118 | b | 4（越界→返回 -1） | 0x800BD0B4 |

唯一传 2 的地方就是 `0x800ABCBC`。而 `0x800ABC8C`：

- 全镜像 ARM `b`/`bl` 立即数扫描：0 命中（`blx rN` 无法静态判定，故另做裸指针搜索）
- 全镜像 4 字节裸指针（含非对齐）搜索 `8C BC 0A 80`：0 命中
- 所在虚表（`0x80577064` 起，类名串 `0x80577048 = "15C3AStateLockCAF"`）中**没有**它；同位置的兄弟 `0x800ABC1C` 在槽 `0x80577090`（+0x2C）里

→ **0x800ABC8C 是孤儿函数（编译产物但无任何引用），SLOW-AF 无法从连续 AF 状态机进入。**

### 1.4 根 B 的可达性：命令/调试面

`0x802FB474(argc, argv)` 是一张纯字符串命令表，表体在 `0x802FC42C`：

```
multi / init / afsta / afsto / s0afsta / slafsta / info / ... / cafset / paf / paf_adj / ...
```

`slafsta` 是第 6 项：`ldr r1,=0x807457E4("slafsta"); strcmp; beq 0x802FBADC` → `CAF::Instance()` → `CAF::SlowAFStart`。

但 `0x802FB474` 本身：

- ARM `b`/`bl` 扫描 0 命中
- movw/movt 0 命中
- 4 字节裸指针 0 命中

→ 该命令处理函数在本镜像里**没有静态可证的注册者**（只能是符号表/调试通道/另一核的命令表登记）。所以「`slafsta` 命令」是存在的入口，但无法从本镜像证明它会被自动调用。

### 1.5 对照组：S0-AF（同族、活的）

`0x800ABC1C`：

```
800ABC24  bl 0x800A8924 ; cmp r0,#0 ; popne   ; 条件1 不满足就不启动
800ABC30  bl 0x800A89A8 ; cmp r0,#0 ; popne   ; 条件2 不满足就不启动
800ABC3C  mov r3,#1 ; strb r3,[r4,#4]          ; this->started = 1
800ABC48  mov r1,#3
800ABC54  b  0x8009B960                        ; CIQIfAf::Start(3) → CAF::S0AFStart
```

它挂在 `C3AStateLockCAF` 的虚表（基址 0x80577064，typeinfo 0x8057705C，类名串 0x80577048 = `"15C3AStateLockCAF"`）**槽 +0x2C** 上（0x80577064+0x2C = 0x80577090），同表还有大量同族状态方法。另有一个同槽位的无参薄包装：

```
800A8688  push {r3,lr}
800A868C  ldr  r3,[r0]
800A8690  ldr  r3,[r3,#0x2c]      ; 同一个槽
800A8694  blx  r3
```

即「状态机 → 虚槽 +0x2C → S0-AF」。**这是"AF 模式驱动"的直接证据**：S0/SLOW 这类模式是 C3A 状态类（ScanAF / LockCAF / Movie…）的方法，不是测光模块的方法。
（保留：0x800A8688 本身也无可证调用者，槽 +0x2C 的实际发起指令未定位；但"该函数是 C3AStateLockCAF 的虚方法"由虚表位置可证。）

两个前置条件也只是状态字节：

```
0x800A8924:  return ([0x8142A60D] != 3)     ; 只有 ==3 才继续
0x800A89A8:  return ([0x8142A605] - 2) <= 1 ; 即 ∈{2,3}
```

即「C3A 某状态 == 3 且 另一状态 ∈ {2,3}」才启动 S0-AF —— 又一次落回"模式/状态驱动"。

---

## 二、判据证据（`CAF::SlowAFStart` 里到底看了什么）

### 2.1 三道门（0x802F64B8 全文可读，帧 0x1c）

```
802F64DC  ldrb r3,[r5,#0x10]      ; r5 = GAF
802F64E0  and  r3,r3,#0x82
802F64E4  cmp  r3,#0x82
802F64E8  beq  0x802F65F0         ; 门1：GAF+0x10 的 bit7|bit1 同时置位
                                  ;      先 bl 0x802F8E7C / 0x802F8D88 再继续

802F6518  ldrb r2,[r5,#0xe]
802F6520  and  r2,r2,#0x40
802F6530  bne  0x802F65B8         ; 门2：GAF+0xe bit6 置位走"查询镜头"分支
...
802F65B8  add  r0,r4,#0x24
802F65C4  bl   0x802FF7DC         ; 查询：0x80234318 / 0x80234A74（镜头命令层，
                                  ;   比较返回码 0x2002 / 0x201A）
802F65C8  ldrb r3,[sp,#0x10]
802F65CC  tst  r3,#1
802F65D0  beq  返回               ; 返回值 bit0 == 0 → 直接放弃启动

802F6534  ldrb r3,[r4,#0x59]      ; 门3：CAF 状态
802F6538  sub  r3,r3,#2
802F653C  cmp  r3,#1
802F6540  bls  0x802F65E4         ; state∈{NORM_DRV(2),FINAL_DRV(3)} → 先 bl 0x802F5F54
                                  ;   （停驱动/清缓存）再继续
802F6544  ldr r0,=0x815502A8
802F6548  bl   0x802AE128         ; 真正置 SLOW 模式
```

`this+0x59` 的语义由 `0x802F3750` 定死：它是 CAF 状态字节，写入前用 `0x802F2E08(this,5,state,buf)` 翻译成名字并打印 `StateChange([%s]->[%s])`。状态名表（case 5）：

| 值 | 名字 |
|---|---|
| 0 | INIT |
| 1 | CONTI_WAIT |
| 2 | NORM_DRV |
| 3 | FINAL_DRV |
| 4 | STOP |
| 5 | COMP |

所以门3 的含义是"镜头正在驱动中（正常驱动/末段驱动）→ 先停下再启动慢速 AF"，**是镜头状态，不是亮度**。

### 2.2 该函数调用的全部符号（无照度量）

```
0x80046484 日志      0x802A9E84 清 AF 信息
0x802AC4FC 状态取值   0x802AE128 置 SLOW
0x802F3750 置 CAF 状态 0x802F5F54 停驱动
0x802F6164 启动后处理  0x802F8D88 / 0x802F8E7C 日志前缀
0x802F92F4 日志       0x802FF7DC 镜头查询
0x8033273C 驱动 SetValue
```

对照：真正的曝光/照度读取（ISO、BV、LV、Tv、AE 结果）都走 `0x8033273C/0x80332948` 的**不同属性号**，而这里用到的属性只有 `0x0405`（`0x802F5F54` 里）与 `0x0422`（`0x802F6558` 前后），语义是 AF/镜头通路控制。**没有任何一处把 AE 结果、ISO、曝光时间或主脸 Y 拉进来做比较。**

### 2.3 暗光判据落在哪：AF 链里没有落点

对本链做"输入全集"归纳，`CAF::SlowAFStart` 的外部输入只有：

1. `GAF+0x10` 的 bit7|bit1（AF 序列状态组合）
2. `GAF+0xe` 的 bit6（AF/镜头使能类标志）
3. `this+0x59`（CAF 状态）
4. 镜头命令层返回码（0x2002 / 0x201A）
5. `GAF+0x24` 区的一次查询结果 bit0

没有第 6 项。**暗光判据不在这条链上。**（`GAF+0xe` bit6 的置位者在 0x802ADACC 的复位段里被 `and #0xfc` 清掉，未见同镜像内的置位点，见"未解项"。）

---

## 三、SLOW-AF 与暗光的关系（明确判定）

**判定：SLOW-AF 由 AF 模块（连续 AF）的状态/命令触发，不是"低照度自动触发"。**

证据链：

1. **命名与归属**：`CAF::SlowAFStart`、`CAF::S0AFStart`、`onStartSlowAF`、`[GROUP] ### SlowAFStart (AF:%d)`。日志里的 `(AF:%d)` 打的就是 CAF 状态字节（NORM_DRV 等）。SLOW/S0 是 CAF 的两个序列类型。
2. **决策函数不看亮度**：CAF 侧唯一的序列决策日志是
   `CAF::Seq[%s] isS0AF[%d] isRestart[%d] isMovie[%d] isStart[%d] wobb_possi[%d]`（0x802F35A4）。
   它的 5 个决策量是：当前序列名、是否 S0、是否 Restart、**是否 Movie**、是否 Start、wobble 可能性 —— 全部是 AF/拍摄模式量。
3. **门控全是状态量**：见 2.1 的三道门。
4. **反证**：若为暗光触发，必然在链上出现 AE 结果比较或阈值常量（BV/LV 门限）。全链（`0x800ABC8C → 0x8009B960 → 0x802AF8D8 → 0x802F64B8 → 0x802AE128`）逐函数检查：无浮点比较、无 AE 结构体访问、无阈值分支。
5. **更硬的负证据**：本镜像里这条链**根本没有活的业务入口**（1.3）。所以"低照度→自动进 SLOW-AF"在 v1.12 里连触发点都不存在。

**因此：不能把 SLOW-AF 当作"暗光增强对焦"的开关来用。** 我们真正能用的暗光对焦入口是别的（见"未解项"第 4 条与下一步建议）。

---

## 四、`Average Y Data of Main Face`（0x80145FC8）的消费者

### 4.1 结论

**只被一个调试打印函数消费。没有进入 AF 决策，也没有证据进入 AE 决策或美颜。**

### 4.2 证据

生产者（真正算平均亮度的地方）是 `0x8014337C`：

```
80143384  ldr r4,[r3,#0x39c]   ; 人脸数
80143390  ldr ip,[r3,#0x398]   ; 主脸索引
801433A0  lsl r5,ip,#2 ; …      ; face 记录步长 0x14
801433E8  mul sb,r2,r5          ; 行步长 * 采样步长
80143400  ldrb r1,[r4,r3]       ; 逐像素累加 Y
8014342C  udivne r2,r2,ip       ; 除以采样点数
80143430  str r2,[r0,#0x934]    ; 写入对象 +0x934
```

唯一调用者：`0x80143D30` @ `0x80143E90`（FD 主脸信息更新流程）。

读者（getter）：`0x80501490`，是**脸部检测接口类**（单例 0x8145A8D8，ctor 0x80142E44 / 0x80142EC4，虚表 0x80585208）的 **+0x54** 槽：

```
80501490  ldr r0,[r0,#0x934]
80501494  bx  lr
```

全镜像只有一处通过该虚槽取值：

```
8014604C  bl  0x80142FA8        ; 取 FD/AE 单例
80146050  ldr r3,[r0]
80146054  ldr r3,[r3,#0x54]     ; ← 主脸平均 Y
80146058  blx r3
80146060  ldr r0,=0x80718CE8    ; "Average Y Data of Main Face(%d)"
80146068  bl  0x80011E70        ; 打印
```

而这个打印函数 `0x80145FC8` 本身就是 FD/AE 信息 dump：它依次打印
`"=== FD INFO ==="`、`"FD 3A Info (result:%d)"`、`start_x/y/w/h`、`FD_width/height`、`"AE Info (result:%d)"`、`Average Y Data of Main Face(%d)`，然后 `bx` 返回，**没有返回值参与任何判断**（函数末尾无 `mov r0, …` 之外的计算，纯日志）。

`0x80145FC8` 自身无 BL 调用者，只被 `movw/movt` 在 `0x80146968` 里取地址（调试命令注册）。

### 4.3 需要排除的假阳性

`0x8032F470 / 0x8032F4A4 / 0x8032F4E4 …` 这些 `[rX,#0x934]` 读取**不是**脸部对象：

```
8032F45C  add r1,sb,r3,lsl #2
8032F460  ldr r1,[r1,#0x938]
8032F468  mul r1,r1,r2          ; r2 = 小数部分
8032F470  ldr r0,[r3,#0x934]
8032F478  mla r0,r0,r2,r1       ; 相邻两项线性插值
8032F488  asr r0,r0,#8
```

这是「同一基址 +0x934 起的 32 位数组，按 8 位小数插值」的查表代码（PAF/镜头增益 LUT），与 0x8145A8D8 那个脸部对象无关。

### 4.4 判定

- 主脸平均 Y：**计算了，但只用于调试打印**。
- 拍后处理（美颜）走的是脸框坐标，不走这个 `+0x934`。
- AE 的人脸加权测光在本镜像里没有读它（该字段的全部读者只有 0x80501490 与上面的 dump）。

---

## 五、`PafAdjust.csv` 的性质判定

### 5.1 0x802A07C4 反汇编要点（帧 0x2FC）

函数体只包含：

- `0x8050A114` = memset，`0x8050B97C` = strcpy
- `0x804F9FB8 / 0x804FA598 / 0x804F9EDC / 0x804FA248 / 0x804FA538 / 0x804FA580 / 0x804F9BA4 / 0x804F9BA8` = 软浮点（dadd/dmul/dcmp/d2i …）
- **0 处** open / fopen / read / write / 文件系统 API
- 4 个**虚调用**（`vtbl+0x18`、`+0x14`、`+0x1c`、`+0x24`）：签名是"把若干 double + 一个字符串交给日志接口"，即 CSV 行写出；具体名字在接口实现里，本函数只做格式化

字面量池（0x802A0DB8 起）：

```
0x815555B8 / 0x81555B08        参数块基址（RAM）
0x8073B658  "   %+dmm"
0x8073B664  "PafAdjust.csv"     ← 仅此一处引用，@0x802A0DC4
0x8073B674  "Result,sensitvityH,sensitvityV,H(-10),H(0),H(+10),V(-6),V(0),V(+6),MaMaxH,..."  ← CSV 表头
0x8073B6FC  "%s,  %d,%d,  %.1f, %.1f, ..."
```

尾部：

```
802A0D54  blx ip                  ; vtbl+0x14：写一行（8 个 double 走栈）
802A0D58  add r0,sp,#0xf8
802A0D5C  ldr r1,=0x8073B664      ; "PafAdjust.csv"
802A0D60  bl  0x8050B97C          ; strcpy 到栈缓冲
802A0D68  mov r1,#8
802A0D78  add r2,sp,#0xf8
802A0D7C  blx r3                  ; vtbl+0x24：把 CSV 名交给日志接口
802A0D88  blx r3                  ; vtbl+0x1c：收尾
```

**它是"把 PAF 调整结果格式化成 CSV 行并交给日志接口"的写出函数，不是加载器。**

### 5.2 调用上下文直接给出定性

`0x802A07C4` 的唯一调用者是 `0x802A1120`（PAF 工厂调整主流程），调用点 `0x802A17D4`：

```
802A17B0  ldr r3,[r7] ; mov r1,#8
802A17C0  ldr r2,=0x8073B9A8     ; "[WRITE CSV] PAF_Misalign_Adj"
802A17C8  blx r3                 ; 设字段
802A17CC  mov r0,r7
802A17D4  bl   0x802A07C4        ; → 写 PafAdjust.csv
```

同函数内的其它日志：`[START] PAF_Misalign_Adj`、`[START] PAF_JIG_Calib`、`[CALC]`、`[CHECK]`、`[DUMP]`、`[WRITE FLASH] PAF_Misalign_Adj`、`[WRITE CSV] PAF_JIG_Calib`。

`0x802A0DDC` 是同一套的调试变体：`movw/movt r1 = 0x8073B770 ("PafAdjust_d.csv")` → `vtbl+0x18`，然后从 `0x815555B8` 逐字段读 double 打出来（`PafAdjust_d` = debug）。

同类 CSV 名在镜像里成族出现（`adj_flag.csv`、`af_read.csv`、`ois_read.csv`、`shutter.csv` …），都是工厂调整工序的输出。

### 5.3 参数实体在哪 / 能不能像 LUT 那样运行时改写

- 参数块：**`0x815555B8`** 与 **`0x81555B08`** 两个 RAM 全局（0x8029F000–0x802A1800 区间共 8 个函数引用）。
- 这两个地址 **0x8155xxxx 超出镜像末端 0x80B84280** → 属于 RAM/BSS，运行时由代码填充（标定值/默认值），**不是 .rodata 常量**。
- 读取方式：`ldrd r0,r1,[base+0x4e0 / 0x508 / 0x530 / 0x558 / 0x570 / 0x588]`（double）。
- 存在 `[WRITE FLASH] PAF_Misalign_Adj` 分支 → 这份 RAM 数据会被写回 flash 标定区。

**判定：**

| 问题 | 结论 |
|---|---|
| CSV 是否运行时从文件加载 | **否**。无任何 open/fopen；字符串只出现在"设日志名 + 写行"里 |
| 这张表是不是编译期常量表 | **不是**。数值在 RAM（0x815555B8/0x81555B08），由运行时数据填充 |
| 能否像 LUT 那样运行时改写 | **能**（RAM 结构体，改内存即可；也可改其 flash 标定源） |
| 能否把 CSV 当"可改入口" | **不能**。它是产物/日志名，喂它改不了参数 |

---

## 六、未解项

1. **`GAF+0xe` bit6 的置位者未定位**。写者只有 0x802AC92C(bfi bit0)、0x802ADBB8(`and #0xfc` 清零)、0x8033D820(写整字节)，同镜像内没找到把 bit6 置 1 的代码。因此 `0x802FF7DC` 那道"镜头就绪"门在 v1.12 里可能永远不生效。
2. **`GAF+0x10` 的 bit7|bit1（0x82）语义未定**。同结构里被别的函数按 0x08/0x10/0x20/0x40/0x60 位分别使用（PAF/CAF/wobble 相关），这是 AF 序列状态字，不是照度量 —— 这点确定，但每一位的名字没有符号可证。
3. **命令表 0x802FB474 的注册者**。本镜像 BL/BLX/裸指针/movw 全 0 命中；可能由符号表、调试口、或另一核的命令表登记。若真机上有串口/USB 调参通道，`slafsta` 是可用的人工触发点（但属调试面，不是产品行为）。
4. **另一核是否发 `slafsta`**。若 AF 决策在别的核/别的镜像里，它可能通过这条字符串命令驱动 SLOW-AF。要证实需拿到该核的镜像或抓命令通道。
5. **`0x800ABC8C` 为何成为孤儿**。与 `0x800ABC1C`（S0，进了虚表）是孪生函数，只差 `mov r1,#2 / #3`。要么是编译期被裁掉槽位，要么这条产品路径在 NX500 上被关闭（S0 顶上）。这解释了为什么 SLOW-AF 日志在真机上可能从不出现。
6. **`0x8029xxxx` 的 PAF 参数初始化路径未定位**（谁把 flash 标定读进 0x815555B8）。这决定了"改 RAM"是易失还是能固化。

---

## 附：复现方法

```bash
cd E:/NXWA
python tmp_afaddr/af.py fn  802F64B8 0x160      # CAF::SlowAFStart 全文
python tmp_afaddr/af.py call 802AE128           # chunk 内 BL 调用者
python tmp_afaddr/fullcall.py 802AE128 802F64B8 8009B960 802FB474   # 全镜像 BL
python tmp_afaddr/fullxr.py 807457E4 802FB474   # 命令名/函数 movw+字面量+裸指针
python tmp_afaddr/af.py fn  8009B960 0x80       # switch(mode) 跳转表 0x8009B9B0
python tmp_afaddr/af.py fn  80145FC8 0x120      # FD/AE info dump
python tmp_afaddr/af.py fn  802A07C4 0x2A0      # PafAdjust.csv 写出函数
```

脚本：`tmp_afaddr/af.py`（fn/call/bt/xr/s）、`fullcall.py`（全镜像 BL）、`fullxr.py`（全镜像 movw/movt + 4 字节引用）、`gaf.py`（GAF 寄存器污点）。
