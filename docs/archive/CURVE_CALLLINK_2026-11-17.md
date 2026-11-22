# NX500 Tone Curve（Gamma）参数写入路径 —— 静态调用链分析

- 日期：2026-11-17
- 目标固件：`E:/NXWA/fw/v1.12/p7_wrapped.elf`
- 目标函数：`0x8017CF88`（壳）/ `0x8049E4D4`（函数体，`d5_ipc_set_subparam_tc`）
- 方法：纯静态反汇编（capstone 5.0.7 / CS_ARCH_ARM / CS_MODE_ARM），不连接相机，不执行固件

---

## 0. 结论摘要（先看这里）

| 问题 | 结论 |
|---|---|
| 写入是否在 liveview / 录像的**每帧**流程里 | **否**。全镜像只有 3 个调用点，全部不在任何循环体内，且没有一处在帧回调 / EOF / VSYNC 处理链上。 |
| 属于哪条流程 | ① **初始化 / 开机一次**（liveview 后端 `Ipc_Initialize()` 内的一次性参数下发）；② **`gamma.txt` 文件加载**（调试 / 产线用曲线文件解析，非运行时帧流程）。 |
| 能否做实时预览热更新 | **"每帧自动"不行；"按需热更新"可行**。函数本体就是一次纯 DDR 拷贝（108 字），无状态、可重入、幂等；它不要求每帧调用，改完参数后调用一次即可。但固件自身没有把这条链路挂到帧流程上，要做热更新**必须自己新增调用点**（并处理参数集提交）。 |
| A040 / A042 的 11 点曲线消费者 | **零消费者**。本镜像内不存在该标签的字符串、常量表或解析代码（详见第 6 节，含覆盖范围与搜索方法）。 |

**最强单条证据**：全镜像 12,076,672 字节做 `b / bl / blx` 全量扫描（含所有条件码），指向 `0x8017CF88` 的只有 3 条指令；对 3 个调用点所在函数做回边（backward branch）分析，**包围调用点的循环数 = 0**。即一次函数调用最多写一次曲线，绝无每帧重写。

---

## 1. 环境与地址映射校验

```
ELF32 / EM_ARM(0x28) / 单 PT_LOAD
  p_offset = 0x00000000 + 0x1000
  p_vaddr  = 0x80000000
  p_filesz = 0x00B84280   (= 12,076,672)
文件大小    = 0x00B85280   (段头 0x1000 + 段体)
```

映射公式 `file_off = addr - 0x80000000 + 0x1000` 已验证：

| 断言 | 验证 |
|---|---|
| `0x807212F8` = `"CDD_ipc_If"` | file_off `0x7222F8` → `CDD_ipc_If\0\0d5_ipc_register_handler(...` ✔ |
| `0x80721DFC` = `"d5_ipc_set_subparam_tc(%d,0x%08X)\n"` | file_off `0x722DFC` ✔ |
| `0x8017CF88` 是可执行 ARM 代码 | file_off `0x17DF88` → `30 40 2d e9` = `push {r4,r5,lr}` ✔ |

无 section header（`e_shoff = 0`），无符号表 —— 所有函数名均靠镜像内的调试字符串反推（见第 5 节"定名方法"）。

---

## 2. 目标函数身份确认

### 2.1 壳函数 `0x8017CF88`

```
8017CF88  push {r4,r5,lr}
8017CF8C  sub  sp, sp, #0xc
8017CF90  mov  r4, r1            ; r4 = src(108 字缓冲)
8017CF94  mov  r5, r0            ; r5 = idx(子参数槽号)
8017CF98  bl   #0x80173C88       ; 取日志上下文
8017CF9C  movw r2, #0x12F8 / movt r2, #0x8072   ; r2 = 0x807212F8 "CDD_ipc_If"
8017CFA0  movw r3, #0x1DFC / movt r3, #0x8072   ; r3 = 0x80721DFC "d5_ipc_set_subparam_tc(%d,0x%08X)\n"
8017CFAC  mov  r1, #0x20000003                  ; 日志级别/通道
8017CFB8  bl   #0x80173F34       ; 调试输出
8017CFBC  mov  r0, r5
8017CFC0  mov  r1, r4
8017CFC4  add  sp, sp, #0xc
8017CFC8  pop  {r4,r5,lr}
8017CFCC  b    #0x8049E4D4       ; ← 尾跳，真实函数体
```

与已知事实 1 一致。壳只做日志，参数原样透传。

### 2.2 函数体 `0x8049E4D4` = `d5_ipc_set_subparam_tc`

```
8049E4D4  cmp   r1, #0                   ; src == NULL ?
8049E4D8  push  {r3,r4,r5,lr}
8049E4DC  ldrne r5, [pc,#0x48]           ; → 字面量池 0x8049E52C = 0x813EBF50
8049E4E0  movne r3, #0
8049E4E4  beq   #0x8049E514              ; NULL → 报错返回 -1
8049E4E8  asr   r4, r3, #2
8049E4EC  add   r2, r3, #0x1F00
8049E4F0  ldr   ip, [r5, r0, lsl #2]     ; ip = base_table[idx]
8049E4F4  ldr   r4, [r1, r4, lsl #2]
8049E4F8  add   r3, r3, #4
8049E4FC  add   r2, r2, #0x28            ; r2 = 0x1F28 + offset
8049E500  cmp   r3, #0x1B0               ; 0x1B0 / 4 = 108 次
8049E504  str   r4, [r2, ip]             ; 目标：base_table[idx] + 0x1F28 + i*4
8049E508  bne   #0x8049E4E8
8049E50C  mov   r0, #0
8049E510  pop   {r3,r4,r5,pc}
8049E514  ...  ldr r1,=0x806E45DC ("d5_ipc_set_subparam_tc"), r2=#0x5E0(1504 行)
8049E520  bl  #0x804D9724                ; 断言/日志
8049E524  mvn r0, #1
8049E528  pop {r3,r4,r5,pc}
```

要点：

- **签名**：`int d5_ipc_set_subparam_tc(int subIdx, const uint32_t *curve108)`。
- **写入量**：`0x1B0 / 4 = 108 字 = 432 字节`，逐字复制，无转换、无插值。
- **目标地址**：`u32[0x813EBF50 + subIdx*4] + 0x1F28`。`0x813EBF50` 超出镜像末尾（镜像止于 `0x80B84280`），属 **BSS/RAM 运行期基址表**，由初始化代码填充。
- **无锁存**：函数体内没有任何寄存器写、没有 barrier、没有 `d5_ipc_*_update` 类调用 —— 只是把数据写进 DDR 参数槽。提交/生效由同家族另一步（`d5_ipc_update_sram_ip` @ `0x804971BC`、`d5_ipc_set_preset_subtop_ctrl` 等）负责。
- 自身错误日志里引用了符号名字符串 `0x806E45DC = "d5_ipc_set_subparam_tc"`，**这是函数体自证身份的直接证据**（不是靠调用壳推断的）。

### 2.3 基址表 `0x813EBF50` 交叉验证（复核已知事实 3）

- 全镜像原始 dword 命中 `0x813EBF50`：**108 处**。
- 落入的函数（回溯到最近 `push {...,lr}` 序言）：**100 个**，地址区间 `0x804971BC`（`d5_ipc_update_sram_ip`）… `0x804A4A44`（`d5_ipc_set_subtop_ctrl_oic_mtf_unit_dist`）。
- 全部属于 `d5_ipc_get/set_subparam_*` / `d5_ipc_*_subtop_ctrl_*` 家族 —— 与已知事实 3 完全吻合。

---

## 3. 调用点全枚举（方法 + 覆盖范围）

### 3.1 扫描定义

对 `.text` 全段 3,019,712 个 4 字节对齐字逐一判定：

```
B   / BL  : cond != 0xF 且 bits[27:25] == 0b101
            target = addr + 8 + SignExtend(imm24) << 2
BLX(imm)  : cond == 0xF（单独统计；其目标必进 Thumb，落到 0x8017CF88 这种 ARM 偶数地址在语义上不成立）
```

另外补充三道交叉检查，防止漏网：

1. **条件分支**：判定不区分 cond，`BLEQ / BLNE / BLGT / BLCC ...` 全部计入（历史上漏过的是这一类）。
2. **尾跳**：`b` 型无条件跳转同样计入（同族壳 `0x8017CF40/84/3C` 就是用 `b` 尾跳进函数体的）。
3. **间接引用**：全镜像原始 dword 搜索 `0x8017CF88`（小端 `88 CF 17 80`）→ **0 命中**，说明没有代码把该地址装进字面量池或跳转表，排除 `blx rN` 型调用。

### 3.2 结果：仅 3 个调用点

| # | 调用点 | 指令 | 所属函数 | 调用前 3 条指令（上下文） |
|---|---|---|---|---|
| 1 | `0x801377B4` | `bl 0x8017CF88` | `0x80137514` | `mov r0,r4` / `ldr r1,[pc,#0x5fc]` / — |
| 2 | `0x8038F868` | `bl 0x8017CF88` | `0x8038DFE4` | `strh r3,[r8,#0xf4]` / — / — |
| 3 | `0x80390A18` | `bl 0x8017CF88` | `0x8038F90C` | `strh r3,[sl,#0xcc]` / — / — |

> 注：`0x801377B4` 那条不是"无条件 bl 漏条件码"的问题，而是**同一段线性下发序列里**紧跟着 `wb / lcac / r_b_scale / sig / sra / ...` 一长串同族 setter，tc 只是其中之一。

### 3.3 壳的唯一性复核

对**函数体** `0x8049E4D4` 再做一次同样的全镜像扫描：只有 1 条指令指向它 —— `0x8017CFCC b 0x8049E4D4`（即壳自己的尾跳）。

于是完整度为：
```
3 个 bl → 0x8017CF88 → 1 个尾跳 → 0x8049E4D4
```
不存在"绕过壳直接进函数体"的旁路。**"这条写入路径在全镜像里的入口数 = 3"是可以封口的结论。**

---

## 4. 调用树

### 4.1 链路 A —— 初始化（liveview 后端开机参数下发）

```
CBackend_Ipc_View_Sra 的虚表槽 +0x24
  0x8059259C  ──►  0x801D9D38   Ipc_Initialize()                      [L3]
                     │  (product/Backend/IPC/ViewSra/CBackend_Ipc_View_Sra.cpp)
                     ▼
                  0x8012A068   CBackend_Ipc_Param_Base::InitOnce()      [L2-a]
                     │  once-guard: ldrb r5,[flag]; cmp r5,#0; beq→跳过
                     │  日志 tag = "CBackend_Ipc_Param_Base"
                     ▼
                  0x80137514   IpcParam::InitGroup(int group)            [L1]
                     │  switch(group) {0,1,2}；3 组各下发一遍
                     │  先 ldrb→cmp 的"已初始化"标志位守卫
                     │  顺序调用 d5_ipc_set_subparam_wb/lcac/r_b_scale/sig/sra/.../tc
                     ▼
                  0x8017CF88   (日志壳 d5_ipc_set_subparam_tc)          [L0]
                     ▼
                  0x8049E4D4   108 字 → base[idx]+0x1F28               [BODY]
```

旁路入口（同一 L1）：

```
  0x80137E88   CBackend_Ipc_Param_Base::InitOnce2()   [L2-b]
                 once-guard: ldrb r4,[r6,#8]; cmp r4,#0
                 同样 3 次 bl 0x80137514（0x80137F44 / 0x80137F50 / 0x80137F5C）
  0x801CD098   薄封装：push {r3,lr}; bl 0x8012A068; return 0
                 （被 0x80592094 处的 CBackend_Ipc 虚表引用）
```

### 4.2 链路 B —— `gamma.txt` 曲线文件加载（同族 C++ 类 `CIpcGammaD5`）

```
  0x8038DFE4   CIpcGammaD5 虚表槽 +0x14  (vtable 0x8062F6B0, RTTI "11CIpcGammaD5")
                 sprintf(buf, "%s/%s", dir, "gamma.txt")       ; 0x80754170 / 0x80754168
                 ParseParam(buf)                               ; 0x803C5A10
                 GetValue(section="GAMMA_INDEX")                ; 0x803C5B74 / 0x8075417C
                     │
       ┌─────────────┴──────────────────────────────┐
       │ 路径 B1：解析完直接下发                      │ 路径 B2：走虚函数
       ▼                                            ▼
  0x8038F868  bl 0x8017CF88                    0x8038E084  ldr r3,[r4] / ldr r3,[r3,#0x18] / blx r3
       │                                            ▼
       │                                    0x8038F90C  CIpcGammaD5 虚表槽 +0x18（曲线装配）
       │                                            │ 读打包位域 0x803C55B8，按 0..4 / 0xC5..0x14C 分支
       │                                            ▼
       │                                    0x80390A18  bl 0x8017CF88
       ▼                                            ▼
  0x8049E4D4   108 字写入                       0x8049E4D4
```

关键交叉验证：`0x8038DFE4` 自己就调用了 `0x8038F90C`（`0x8038E074..0x8038E084` 的 `ldr r3,[r4]; ldr r3,[r3,#0x18]; blx r3`），即两条 B 路径**同源**，都是"读 gamma.txt → 装配成 108 字 → 下发"。

---

## 5. 每个节点的定名证据

> 镜像被 strip，函数名全靠镜像内的调试字符串 / RTTI / 文件路径反推。下表每一行都给出"证据字符串 + 证据地址 + 引用指令地址"三元组，可复现。

| 地址 | 判定名 | 证据字符串（地址） | 引用点 |
|---|---|---|---|
| `0x8049E4D4` | `d5_ipc_set_subparam_tc` | `"d5_ipc_set_subparam_tc"`（`0x806E45DC`）；`"d5_ipc_set_subparam_tc(%d,0x%08X)\n"`（`0x80721DFC`） | `0x8049E514`（字面量池 `0x8049E530`）、壳 `0x8017CFB8` |
| `0x8017CF88` | 上述函数的日志壳 | `"CDD_ipc_If"`（`0x807212F8`） | `0x8017CFB0` |
| `0x80137514` | `IpcParam::InitGroup` | `"IpcParam"`（`0x807159A8`）；函数体内 20+ 处 `bl 0x8017Cxxx` 对应 `"d5_ipc_set_subparam_wb(%d, %d, 0x%08X)"`（`0x80721940`）等同族格式串 | `0x801378E8`、`0x801378F8`… |
| `0x8012A068` / `0x80137E88` | `CBackend_Ipc_Param_Base::InitOnce` | `"CBackend_Ipc_Param_Base"`（`0x807158A8`）；类名表 `"23CBackend_Ipc_Param_Base"`（`0x805849FC`）、`"22CBackend_Ipc_Parameter"`（`0x80584A38`） | `0x8012A1A4`、`0x80137F84` |
| `0x801D9D38` | `Ipc_Initialize()` | `"Ipc_Initialize"`（`0x80592168`）；`"(Initialize) IPC Param Ptr NULL Error!"`（`0x801D9D38` 自身）；文件 `product/Backend/IPC/ViewSra/CBackend_Ipc_View_Sra.cpp`（`0x8072EBC4`） | `0x801D9BA4`、`0x801D9C80` |
| `0x80592574` | `CBackend_Ipc_View_Sra` 虚表 | RTTI 名 `"21CBackend_Ipc_View_Sra"`（`0x805925F0`） | 类型信息 `0x80592608` |
| `0x8038DFE4` | `CIpcGammaD5::LoadGammaFile`（+0x14 槽） | `"gamma.txt"`（`0x80754170`）、`"GAMMA_INDEX"`（`0x8075417C`）、`"%s/%s"`（`0x80754168`） | `0x8038DFF8`、`0x8038E060`、`0x8038E004` |
| `0x8038F90C` | `CIpcGammaD5::ApplyGammaCurve`（+0x18 槽） | RTTI `"11CIpcGammaD5"`（`0x8062FCE4`）；vtable `0x8062F6B0` | ctor `0x8038DA20` 写 vptr `0x8062F6B0`；同族 `"20CIpcParamMultiNanrD5"`（`0x8062F650`）、`"11CIpcParamD5"`（`0x8062F614`） |
| `0x8038DA5C` | `CIpcGammaD5` 构造 | vtable 自证 | `0x8038DB08` |
| `0x8038D358` | `CIpcParamD5` 构造（内含 CIpcGammaD5 成员） | vptr → typeinfo `"11CIpcParamD5"` | 字面量池 `0x8038D534` |

### 5.1 定名方法（可复现）

1. **函数自名**：镜像 `0x806E0000–0x80740000` 是一张"函数名 + 文件:行"的字符串池（`_ipc_get_subparam_*`、`d5_ipc_set_subparam_*` 等）。几乎每个函数在 **assert/error 分支**里用 `ldr rX,[pc,#imm]` 加载自己的名字。于是：
   - 建 `字面量池地址 → ldr 指令地址` 映射；
   - 建 `dword → 槽地址` 映射（槽值落在 `0x80560000–0x80740000`）；
   - 两级反查即得 `函数地址 → 名字`。本次共解出 **4,381** 个函数名（`tools/names.json`）。
2. **C++ 类名**：vptr → `[vptr-4]` = typeinfo → `[typeinfo+4]` = Itanium 风格名字（`"11CIpcGammaD5"` 是 `<长度><名字>`）。
3. **行为旁证**：本报告所有关键节点都同时有"字符串"+"行为"两条独立证据（例如 `0x8038DFE4` 既有 `gamma.txt` / `GAMMA_INDEX` 字符串，也有 `sprintf → ParseParam → GetValue` 的调用序列）。

---

## 6. 路径分类结论

| 调用点 | 所属函数 | 分类 | 判据（靠什么判的） |
|---|---|---|---|
| `0x801377B4` | `0x80137514` `IpcParam::InitGroup` | **初始化 / 开机（每次 liveview 后端 Init 一次）** | ① 上游 `Ipc_Initialize()` 名字 + 文件名 `CBackend_Ipc_View_Sra.cpp`；② 上游两级 `ldrb flag; cmp #0; beq` 一次性守卫；③ 本函数 `switch(group){0,1,2}` 三组全量下发；④ 无循环包围调用点；⑤ 一次调用下发 wb/lcac/sig/sra/... 整个家族 —— 典型"上电铺参数"。 |
| `0x8038F868` | `0x8038DFE4` `CIpcGammaD5::LoadGammaFile` | **文件加载 / 调试-产线（on-demand）** | ① 字符串 `gamma.txt` + `GAMMA_INDEX`；② 调用序列 `sprintf("%s/%s") → ParseParam → GetValue(section)`；③ 解析函数返回 0 即提前 `return`（`0x8038E014 cmp r0,#0; 0x8038E018 bne` 才继续），说明是可选的外部配置；④ 无循环包围调用点。 |
| `0x80390A18` | `0x8038F90C` `CIpcGammaD5::ApplyGammaCurve` | **派生自上一行（曲线装配后下发）** | ① 唯一镜像内调用者是 `0x8038E084`（虚表 +0x18），属 `0x8038DFE4` 的函数体；② 函数把打包位域（`0x803C55B8` 取位域）展开成 108 字曲线；③ 无循环包围调用点。 |

**没有一条落在 still capture、liveview 每帧、video record 上。**

补充说明（容易误判的点）：

- `0x801D9D38` 位于 `CBackend_Ipc_View_Sra`（liveview 后端）里，但它是 `Ipc_Initialize`，**每次 liveview 会话建立时执行一次**，不是每帧。同理 `0x801CD098` 在 `CBackend_Ipc`（基类）虚表里，也是创建期回调。
- `0x8038F90C` 内部确有多个循环（39 条回边），但都是"把 11/33/108 点装配成 LUT"的**数据循环**，不是帧循环；其调用点 `0x80390A18` 在循环外。
- `0x8049E4D4` 自身有一个 108 次迭代的拷贝循环 —— 那是**单次写入的展开**，不是每帧重复。

---

## 7. "每帧"证据检查（Q3）

| 检查项 | 方法 | 结果 |
|---|---|---|
| 调用点是否被循环包围 | 对 3 个函数各做回边枚举（`b<cond>` 且 target < addr 且不是 bl），判断是否存在 `target ≤ call ≤ addr` | `0x80137514`：16 条回边，包围 0；`0x8038DFE4`：61 条回边，包围 0；`0x8038F90C`：39 条回边，包围 0 |
| 是否有帧回调把这条链挂上去 | 全镜像 raw dword 搜 `0x8017CF88` / `0x8038DFE4` / `0x8038F90C` / `0x80137514` | `0x8017CF88`、`0x80137514`：**0 命中**（未出现在任何回调表/函数指针表）；`0x8038DFE4`、`0x8038F90C`：各自只在 `CIpcGammaD5` 虚表 `0x8062F6C4` / `0x8062F6C8` 出现一次 |
| 是否有 EOF / VSYNC / FRAME_DONE 关联 | 检查同家族字符串与注册点：`"d5_ipc_register_handler(CTRL0_CS0_FRAME_DONE_INTR, 0x%08X)"`（`0x80721434`）等 | 寄存器回调注册全部落在 `d5_ipc_register_handler`（`0x8017Cxxx` 系列），其回调集合里**不含**上述 4 个地址；tc 写入与中断注册无交集 |
| 是否存在定时器 / 周期任务 | 同上（无函数指针引用即无注册可能） | 无 |
| 虚表槽调用统计 | 全镜像扫描 `ldr rN,[rM]; ldr rN,[rN,#0x18]; blx rN`（窗口 14 条指令，331 处命中） | `CIpcGammaD5 +0x18`（`0x8038F90C`）**仅被 `0x8038E084` 调用**，且 `0x8038E084` 就在 `0x8038DFE4` 内 |

结论：**不存在每帧证据**。这条链路是"事件驱动 + 一次性"的。

---

## 8. A040 / A042 的 11 点曲线（Q4）

### 8.1 结论

**零消费者。** 本镜像内找不到 A040 / A042 标记的 11 点曲线的任何读取、解析或使用代码。

### 8.2 搜索方法与覆盖范围（全量，非抽样）

| # | 搜索对象 | 覆盖 | 结果 |
|---|---|---|---|
| 1 | ASCII 子串 `A040` / `A042` / `a040` / `a042` | **整个 12,076,672 字节文件**（`bytes.find` 全量，非仅解包段） | **0 命中** |
| 2 | 可打印串中含 `11point` / `11_point` / `11pt` / `Curve11` / `ToneCurve` / `tone_curve` | 全文件 | **0 命中**；仅命中 `EE_GAMMA_COMP_00..18`、`GAMMA_INDEX`、`gamma.txt`、`MOVIE_GAMMA_CONTROL_*` |
| 3 | 常量 `0x0000A042`（小端 u32，等价于大端 u16 `A0 42`） | 全文件 | **0 命中** |
| 4 | 常量 `0x0000A040`（u32） | 全文件 | 10 命中，全部落在**地址斜坡表**（`0xA000,0xA010,…,0xA0F0,0xA0FF` 递增序列，见 `0x80897E40` / `0x808AB240` / `0x80B78240`），与 MakerNote 无关 |
| 5 | u16 `0xA042` 与 `0xA040` / `0xA041` 的**共现**（±64 字节，检测"标签表"） | 全文件 | 27 组共现，**全部位于代码段且地址非 4 字节对齐**（如 `0x80019299`、`0x80039EF5`），是指令字节的巧合，无一组构成 `{tag, handler}` 结构 |
| 6 | MakerNote 解析器本体 | 通过自名表定位 | `CIQIfMakerNote`（`0x80575D74`），4 个方法：`GetDataBeforePP`(`0x8009FC8C`)、`GetDataBeforeIPCS`、`GetDataAfterIPC`(`0x8009FDB0`)、`GetOffset`(`0x8009FE4C`)、`GetSize`(`0x8009FF60`) |
| 7 | MakerNote 标签寻址机制 | 反汇编 `GetOffset` | **标签不是 EXIF tag 号**：`cmp r4,#7; ldrls pc,[pc,r4,lsl#2]` —— 只按 **0..7 共 8 个块** 分派，转 `CIQ_MakerNoteIF::GetOffset_0..7`（`0x802AF64C / 664 / 67C / 694 / 6AC / 6C4 / 6DC`）。整个 MakerNote 在本镜像里是 **8 块不透明数据**，逐字段（0xA040/0xA042 级）解析不在这里。 |
| 8 | 11 点（`#0xb`）计数循环 | 在 gamma 相关代码 `0x8038D000–0x80391000` 全段扫描 | 仅 2 处 `#0xb` 立即数，均非计数循环（`ldrb sb,[r3,#0xb]` 结构体取字段、`movt r3,#0xb` 构造常量） |

### 8.3 解读与边界

- 本镜像里 **tone curve 的唯一外部输入是 `gamma.txt` 的 `GAMMA_INDEX` 段**，不是 MakerNote。
- MakerNote 侧的 A040/A042 若要被消费，只有三种可能，本镜像都排除：
  1. 以字面 tag 号出现 → 第 3/4/5 项已排除；
  2. 以字符串出现 → 第 1 项已排除；
  3. 通过 `eIQIF_MAKERNOTE_ID` 枚举分派 → 第 7 项证明该枚举只有 8 个"块"，无逐 tag 分派。
- **边界声明（避免"选择性解包就说没人用"）**：以上结论**仅对 `p7_wrapped.elf` 这一个镜像成立**。MakerNote 的写入侧（拍照时把 IQ 参数写进 JPEG）与部分后处理可能位于 `dicam` / kernel 分区，不在本次范围内；若 A040/A042 的消费者在那些分区，本报告不能排除。

---

## 9. 对"实时预览热更新"的工程结论

1. **固件原生行为**：曲线只在两个时机被下发 —— ① liveview 后端 `Ipc_Initialize()` 一次；② 有人调用 `gamma.txt` 加载。**取景每帧、录像每帧都不会重推曲线。**
2. **函数本体的性质**（决定改造成本）：
   - `d5_ipc_set_subparam_tc(idx, buf108)` 是**纯拷贝**：无缓存、无状态机、无锁存、无返回值依赖（成功返回 0，`buf==NULL` 返回 -1 并打 assert）。
   - 因此它**可以安全地被每帧调用**（幂等），但也没必要 —— 每次调用都会完整重写 432 字节到 DDR 参数槽，属于纯带宽浪费。
3. **热更新可行的最小方案**：
   - 在任意"参数已变更"的位置（例如 UI 改曲线、或你自己的帧钩子）调用一次 `d5_ipc_set_subparam_tc(ch, curve108)`；
   - **注意 `ch`（`subIdx`）**：`CIpcGammaD5` 有两个实例，构造时 `strb 0` / `strb 1` 区分（`0x8038552C` / `0x80385530`），取通道号的是 `0x803C59F0`（读 `[this+0xC]` 的 `[+0xD]` 字节，归一化为 0/1）。**要热更新的通道必须与当前取景通道一致**，否则改的是另一条通路。
   - **提交**：本函数只写 DDR，不触发参数集生效。同家族存在 `d5_ipc_update_sram_ip`（`0x804971BC`）与 `d5_ipc_set_preset_subtop_ctrl`（`0x8017C…`）等提交/切换入口。真正的"下一帧生效"取决于 ISP 参数集是否被重新 latch —— 这一点静态分析无法确证，需要在 `d5_ipc_update_sram_ip` / `set_preset_subtop_ctrl` 的调用时机上继续验证，或用动态手段确认。
4. **一句话**：**"每帧刷新"的现成通路不存在；"改一次、下一帧生效"的通路在函数层面是通的，但需要一个新增调用点 + 一次参数集提交。**

---

## 10. 最短路径（给上层结论用）

**初始化链（最典型的真实路径）：**

```
0x801D9D38  Ipc_Initialize()
      → 0x8012A068  CBackend_Ipc_Param_Base::InitOnce()   (once-guard)
      → 0x80137514  IpcParam::InitGroup(group)
      → 0x8017CF88  d5_ipc_set_subparam_tc 日志壳
      → 0x8049E4D4  写 108 字到 base[idx]+0x1F28
```

**gamma.txt 链：**

```
0x8038DFE4  CIpcGammaD5::LoadGammaFile(".../gamma.txt", "GAMMA_INDEX")
      → 0x8038F90C  CIpcGammaD5::ApplyGammaCurve（虚表 +0x18）
      → 0x8017CF88  → 0x8049E4D4
```

---

## 附录 A：本次使用的脚本（`E:/NXWA/tools/`）

| 文件 | 作用 |
|---|---|
| `nx.py` | ELF 地址映射、`u16/u32`、capstone 封装 |
| `blscan.py` | 全镜像 `b/bl`（含所有条件码）目标扫描（**已修正 imm24 左移 2 的 bug**，务必以此版为准） |
| `findfn.py` | 由调用点回溯最近 `push {…,lr}` 求函数边界 |
| `litxref.py` | `ldr rX,[pc,#imm]` 字面量池 → 目标地址映射 |
| `mwxref.py` | `movw/movt` 常量构造 → 目标地址映射 |
| `symmap.py` / `dumpnames.py` | 两级反查（槽 → ldr → 函数），批量解出 4,381 个函数名（`names.json`） |
| `vdispatch.py` | `ldr rN,[rM]` + `ldr rN,[rN,#0xNN]` + `blx rN` 虚表分派扫描 |

## 附录 B：已知局限

1. 镜像 strip，函数名来自调试字符串推断（证据强度：高，均有字符串 + 行为双证；但仍属"推断"而非符号表）。
2. `0x8038DFE4`（`LoadGammaFile`）在镜像内**没有找到直接的 `bl` 调用者**，只有虚表槽 `+0x14` 引用。这说明它要么由本镜像之外的模块调用，要么在当前配置下就是死代码。无论哪种，都不构成"每帧"证据。
3. 间接调用（`blx rN` 且地址由运行期计算）无法静态穷举；本报告用"全镜像 dword 搜索目标地址 = 0 命中"来兜底，但理论上仍存在"运行时算出 0x8017CF88"的极端可能（未见任何构造该常量的代码）。
4. ISP 侧对 DDR 参数槽的读取时机、参数集双缓冲/latch 行为，超出静态分析能力，需硬件手册或动态验证。
