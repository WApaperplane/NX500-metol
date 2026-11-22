# MCB 协议与命令枚举表

> **2026-11-20 文档合并：** 本文由 2 份同主题记录合并而成，原文按时间顺序保留、未删改，标题层级下调一级。原始文件已归档到 `docs/archive/`，文件名见各节「来源」。

1. MCB 命令号 × 名字 —— 静态枚举全表（2026-10-08 · D4/U02）
2. st 全表 × MCB 协议 × di-camera-app 全量反编译（2026-10-08）

---

## 1. MCB 命令号 × 名字 —— 静态枚举全表（2026-10-08 · D4/U02）

> 来源：`MCB_COMMAND_ENUM_TABLE_2026-10-08.md`（已归档至 `docs/archive/`）

> 承接 [本文 §2](本文 §2) §3.3（当时只有 mcbtest `capture_command_string` 的 ~40 条采样）。
> 本篇给出**静态枚举表的实锤位置**与**8 域 68 条全表**。
> 机器可读全表：`raw8/repro/mcb_command_enum_table.txt`。原始证据：`raw8/repro/capfw/`。

---

### 0. 一句话结论

**静态枚举表不是 `{u16 id, char* name}` 数组，而是导出函数 `capture_command_string`（`libcapture-fw-prod.so`，VA `0x511e0`，2152 B，编译成二分 if-switch）；名字串按"域 + 命令号"顺序连续排在 `.rodata 0xa34b8..0xa39d8`。它在 `OperateCapture@0x46b58` 处经 `mcb_debug_set_command_name_function` 注册进 MCB 层。据此重建出覆盖全部 8 个域、共 68 条命令号↔名字的完整表**（比旧采样 40 条新增 28 条，零冲突）。

- 8 域全覆盖：`SYS_`(7) `STILL_`(14) `LV_`(15) `MOVIE_`(4) `LENS_`(6) `QUICKPB_`(4) `FLASH_`(7) `PRD_`(11) = **68**
- 名字**整串存储**（如 `SYS_CIS_CLEANING` 是单串，非"前缀+后缀"拼接）——此前"名字不是整串存的"猜测**证伪**（见 §4）
- 唯一的空洞：`LENS_ 0x506–0x508`（枚举内无定义，落入 `unknown`）

### 1. 表结构与反汇编证据（硬证据）

#### 1.1 宿主与定位

| 项 | 值 |
|---|---|
| 文件 | `/opt/nxks2/rootfs-112/usr/lib/libcapture-fw-prod.so`（734600 B，ARM EABI5，**stripped**） |
| MD5 | `78765796c0003f1a84c51bc67b3b2ef4`（实测 `md5sum`）|
| 函数 | `capture_command_string`，**导出**符号，VA `0x511e0`，size **2152**（`readelf -sW` 第 247 项）|
| 文件偏移 | `0x511e0`（本 .so 段 VA==文件偏移，无独立基址：`.text` Addr/Off 均 `0x28560`）|
| 名字串池 | `.rodata` VA `0xa34b8`(`SYS_CIS_CLEANING`) … `0xa39d8`(`PRD_ADJUST_REQUEST_DONE`) 连续排列 |
| 字面量池 | `0x5192c..0x51a44`（每个 case 的名字指针来源）|

调用链证据（注册点）：
```
46b50: ldr r3,[r4,r3]; mov r0,r3
46b58: bl 25f50 <mcb_debug_set_command_name_function@plt>   ; ← 注册 capture_command_string
```

#### 1.2 结构 = 二分比较树 + 字面量池

`capture_command_string(int cmd)` 被编译成对命令号的**二分 if-else 树**（不是跳转表、更不是数据表）。每个命中分支形如：

```
51254: beq 515d0            ; cmd == 0x102 → 叶子 515d0
...
515d0: ldr r3, [pc, #856]   @ 51930        ; 取字面量(相对偏移)
515d4: add r3, pc, r3                      ; r3 = 名字指针
515d8: b   51920                           ; 公共返回
```
- 字面量池 `0x5192c..0x51a44` 存的是"相对 add 指令 PC 的偏移"；
  **名字 VA = (add 指令地址 + 8) + 字面量 = (ldr 地址 + 12) + 字面量**。
  验证：`0x515c4 + 12 + 0x00051ee8 = 0xa34b8` = `SYS_CIS_CLEANING` ✅
- 未命中分支统一跳到 `518f4`，用 `sprintf(buf, "0x%x", cmd)` 落 `DAT_000b3180`（即 `unknown` 缓冲）。

#### 1.3 名字串池 = 按枚举序排列（关键旁证）

`.rodata 0xa34b8` 起，名字严格按 `域 → 命令号升序` 连续存放，与"枚举定义序"一致：

```
a34b8 SYS_CIS_CLEANING / a34cc SYS_CONTROL_STROBE / a34e0 SYS_RESET / a34ec SYS_CHANGE_MODE
a34fc SYS_FILEDUMP_ACK / a3510 SYS_POWER_SAVE / a3520 SYS_POWER_OFF
a3530 STILL_START_CAPTURE / … / a3634 STILL_SET_AEL_BV
a3648 LV_START / … / a3738 LV_FD_FREEZE
a3748 MOVIE_START_RECORDING / … / a3790 MOVIE_TOUCH_AF
a37a0 LENS_POWER_ZOOM / … / a37f4 LENS_SET_MF_POSITION
a380c QUICKPB_CHECK_IMAGE / … / a3844 QUICKPB_DELETE_FILE
a3858 FLASH_EXT_HSS_ONOFF / … / a38d8 FLASH_EXT_DISPLAY_FNO
a38f0 PRD_CONTROL / … / a39c0 PRD_ADJUST_REQUEST_DONE
```

#### 1.4 "不存在 {id,name} 表"的反证（硬证据）

- `readelf -rW`：全 .so 共 **1287 条 `.rel.dyn`**（另约 1000 条 `.rel.plt`），其中**指向名字池(0xa34b8–0xa3a00)的重定位 = 0 条**（`raw8/repro/capfw/reldump.txt`）。⇒ 没有任何数据段的指针数组引用这些名字。
- 名字的唯一引用者 = `capture_command_string` 函数内字面量池。
- `libmulticore-bridge.so` 内**无**任何 8 域名字串（全 rootfs grep：仅 `usr/bin/mcbtest` 与 `usr/lib/libcapture-fw-prod.so` 含 `SYS_CIS_CLEANING`）。

### 2. 全表（68 条 · 硬证据）

> 完整机器可读版含 `leaf VA / str VA`：`raw8/repro/mcb_command_enum_table.txt`。下表 `源` = `capture_command_string` 叶子地址 @ `str` 串地址。

#### SYS_（域 0x01，基址 0x100）
| cmd | 名字 | 源 |
|---|---|---|
| 0x101 | SYS_CIS_CLEANING | leaf 0x515c4 / str 0xa34b8 |
| 0x102 | SYS_CONTROL_STROBE | 0x515d0 / 0xa34cc |
| 0x103 | SYS_RESET | 0x515dc / 0xa34e0 |
| 0x104 | SYS_CHANGE_MODE | 0x515e8 / 0xa34ec |
| 0x105 | SYS_FILEDUMP_ACK | 0x515f4 / 0xa34fc |
| 0x106 | SYS_POWER_SAVE | 0x51600 / 0xa3510 |
| 0x107 | SYS_POWER_OFF | 0x5160c / 0xa3520 |

#### STILL_（域 0x02，基址 0x200）
| cmd | 名字 | 源 |
|---|---|---|
| 0x201 | STILL_START_CAPTURE | 0x51618 / 0xa3530 |
| 0x202 | STILL_STOP_CAPTURE | 0x51624 / 0xa3544 |
| 0x203 | STILL_CANCEL_QVIEW | 0x51630 / 0xa3558 |
| 0x204 | STILL_LOCK_AEAF | 0x5163c / 0xa356c |
| 0x205 | STILL_LOCK3A | 0x51648 / 0xa357c |
| 0x206 | STILL_CAF | 0x51654 / 0xa358c |
| 0x207 | STILL_MOVIE_AF | 0x51660 / 0xa3598 |
| 0x208 | STILL_TOUCH_AF | 0x5166c / 0xa35a8 |
| 0x209 | STILL_TRACKING_AF | 0x51678 / 0xa35b8 |
| 0x20a | STILL_SELECT_IMAGE_BEST_FACE | 0x51684 / 0xa35cc |
| 0x20b | STILL_SELECT_FACE_BEST_FACE | 0x51690 / 0xa35ec |
| 0x20c | STILL_RELEASE_BUFFER | 0x5169c / 0xa3608 |
| 0x20d | STILL_PROGRAM_SHIFT | 0x516a8 / 0xa3620 |
| 0x20e | STILL_SET_AEL_BV | 0x516b4 / 0xa3634 |

#### LV_（域 0x03，基址 0x300）
| cmd | 名字 | 源 |
|---|---|---|
| 0x301 | LV_START | 0x516c0 / 0xa3648 |
| 0x302 | LV_STOP | 0x516cc / 0xa3654 |
| 0x303 | LV_FACE_DETECTION | 0x516d8 / 0xa365c |
| 0x304 | LV_SCENE_DETECTION | 0x516e4 / 0xa3670 |
| 0x305 | LV_OBJECT_TRACKING | 0x516f0 / 0xa3684 |
| 0x306 | LV_OPTICAL_PREVIEW | 0x516fc / 0xa3698 |
| 0x307 | LV_MF_ENLARGE | 0x51708 / 0xa36ac |
| 0x308 | LV_IFN_ENABLE | 0x51714 / 0xa36bc |
| 0x309 | LV_PREPARE | 0x51720 / 0xa36cc |
| 0x30a | LV_SET_CENTER_AF | 0x5172c / 0xa36d8 |
| 0x30b | LV_OBJECT_TRACKING_HOLD | 0x51738 / 0xa36ec |
| 0x30c | LV_ZEBRA_CTRL | 0x51744 / 0xa3704 |
| 0x30d | LV_PANORAMA_DISPLAY | 0x51750 / 0xa3714 |
| 0x30e | LV_FD_SELFIE | 0x5175c / 0xa3728 |
| 0x30f | LV_FD_FREEZE | 0x51768 / 0xa3738 |

#### MOVIE_（域 0x04，基址 0x400）
| cmd | 名字 | 源 |
|---|---|---|
| 0x401 | MOVIE_START_RECORDING | 0x51774 / 0xa3748 |
| 0x402 | MOVIE_STOP_RECORDING | 0x51780 / 0xa3760 |
| 0x403 | MOVIE_FRAMING_CAPTURE | 0x5178c / 0xa3778 |
| 0x404 | MOVIE_TOUCH_AF | 0x51798 / 0xa3790 |

#### LENS_（域 0x05，基址 0x500；⚠ 0x506–0x508 空洞）
| cmd | 名字 | 源 |
|---|---|---|
| 0x501 | LENS_POWER_ZOOM | 0x517a4 / 0xa37a0 |
| 0x502 | LENS_SET_OIS | 0x517b0 / 0xa37b0 |
| 0x503 | LENS_MF_PERMISSION | 0x517bc / 0xa37c0 |
| 0x504 | LENS_3D_MODE | 0x517c8 / 0xa37d4 |
| 0x505 | LENS_FW_UPDATE | 0x517d4 / 0xa37e4 |
| 0x509 | LENS_SET_MF_POSITION | 0x517e0 / 0xa37f4 |

#### QUICKPB_（域 0x06，基址 0x600）
| cmd | 名字 | 源 |
|---|---|---|
| 0x601 | QUICKPB_CHECK_IMAGE | 0x517ec / 0xa380c |
| 0x602 | QUICKPB_PB_REQUEST | 0x517f8 / 0xa3820 |
| 0x603 | QUICKPB_ZOOM | 0x51804 / 0xa3834 |
| 0x604 | QUICKPB_DELETE_FILE | 0x51810 / 0xa3844 |

#### FLASH_（域 0x07，基址 0x700）
| cmd | 名字 | 源 |
|---|---|---|
| 0x701 | FLASH_EXT_HSS_ONOFF | 0x5181c / 0xa3858 |
| 0x702 | FLASH_EXT_MANUAL_ONOFF | 0x51828 / 0xa386c |
| 0x703 | FLASH_EXT_MULTI_ONOFF | 0x51834 / 0xa3884 |
| 0x704 | FLASH_EXT_EV | 0x51840 / 0xa389c |
| 0x705 | FLASH_EXT_GROUP_MODE | 0x5184c / 0xa38ac |
| 0x706 | FLASH_EXT_GROUP_EV | 0x51858 / 0xa38c4 |
| 0x707 | FLASH_EXT_DISPLAY_FNO | 0x51864 / 0xa38d8 |

#### PRD_ 生产域（域 0xaa，基址 0xaa00；实号 0xaa01–0xaa0b）
| cmd | 名字 | 源 |
|---|---|---|
| 0xaa01 | PRD_CONTROL | 0x51870 / 0xa38f0 |
| 0xaa02 | PRD_BYPASS_SCRIPT | 0x5187c / 0xa38fc |
| 0xaa03 | PRD_WRITE_RESULT_TO_NAND | 0x51888 / 0xa3910 |
| 0xaa04 | PRD_WRITE_RESULT_TO_CSV | 0x51894 / 0xa392c |
| 0xaa05 | PRD_SET_FOCUS_POSITION | 0x518a0 / 0xa3944 |
| 0xaa06 | PRD_SET_AF_LED | 0x518ac / 0xa395c |
| 0xaa07 | PRD_WRITE_LOG_TO_NAND | 0x518b8 / 0xa396c |
| 0xaa08 | PRD_CAPTURE_YUV | 0x518c4 / 0xa3984 |
| 0xaa09 | PRD_CAPTURE_RAW_SSIF | 0x518d0 / 0xa3994 |
| 0xaa0a | PRD_CAPTURE_RAW_PP | 0x518dc / 0xa39ac |
| 0xaa0b | PRD_ADJUST_REQUEST_DONE | 0x518e8 / 0xa39c0 |

### 3. 与旧采样对比（新增 28 / 纠正 0 / 冲突 0）

旧采样 = `mcbtest` 的 `capture_command_string@0x34e2c`（1868 B，比本表**小**），共 40 条（`raw8/repro/mcb_command_dict_raw.txt`）。

- **本次静态重建 = 68 条**，旧 40 条**全部命中且号一致（0 冲突）**。
- **新增 28 条**（旧 switch 采样缺失——它们多来自二分树的 `bgt/blt` **边界叶子**，旧工只抓了显式 `==` 比较，边界情况漏采）：

| 域 | 新增 |
|---|---|
| SYS_ | 0x103 SYS_RESET、0x105 SYS_FILEDUMP_ACK |
| STILL_ | 0x202 STILL_STOP_CAPTURE、0x204 STILL_LOCK_AEAF、0x206 STILL_CAF、0x20b STILL_SELECT_FACE_BEST_FACE、0x20d STILL_PROGRAM_SHIFT |
| LV_ | 0x303 LV_FACE_DETECTION、0x305 LV_OBJECT_TRACKING、0x307 LV_MF_ENLARGE、0x309 LV_PREPARE、0x30e LV_FD_SELFIE |
| MOVIE_ | 0x403 MOVIE_FRAMING_CAPTURE |
| LENS_ | 0x503 LENS_MF_PERMISSION |
| FLASH_ | 0x702 FLASH_EXT_MANUAL_ONOFF、0x704 FLASH_EXT_EV、0x706 FLASH_EXT_GROUP_EV |
| **PRD_** | **全部 11 条**（0xaa01–0xaa0b；旧采样只标了"PRD_ 若干"无号无全名）|

- **纠正**：旧结论"ID 空间 0xaaXX"→ 精确为 **0xaa01–0xaa0b 密集**；旧"PRD_…"漏名 → 全部补齐、定号。

### 4. 未覆盖 / 推断 / 证伪项

| 项 | 结论 | 类别 |
|---|---|---|
| "名字不是整串存的、前缀+后缀分别存储" | **证伪**。名字均为单条整串（`SYS_CIS_CLEANING` 一个串），无前缀/后缀拆分表；`GetSubIDName` 的"域基址+子号"拼装只存在于 **libmulticore-bridge.so 的 MCC 类别层**（见 §5），与本 8 域无关 | 硬证据（证伪） |
| LENS_ 0x506 / 0x507 / 0x508 | 枚举内无定义，`capture_command_string` 返回 `0x506` 等十六进制 | 硬证据（空洞） |
| 各域未列号（如 SYS_ 0x108+、STILL_ 0x20f+…） | 该函数不覆盖 ⇒ 视为未定义/不存在 | 推断 |
| 现场活跃号 0xB0/0xB1/0xB6 | **不在本表**：它们属于 MCC **类别(CID)** 层，名字在 `libmulticore-bridge.so`（§5） | 硬证据（分层） |

### 5. 分层澄清（避免再混淆）

MCB 有**两个独立的编号空间**，此前文档易混：

1. **业务命令号（本篇 8 域 68 条）**：`SYS_/STILL_/…/PRD_`，名字解析器 = `libcapture-fw-prod.so::capture_command_string`（本表）。这是相机业务通过 MCB 下发的命令。
2. **传输类别号 CID（0x10–0xF5，33 条）**：名字解析器 = `libmulticore-bridge.so::CIPCCDriverIf::GetCIDName(int)` @ VA `0x5304`（size 1512，readelf -sW；同为 if-switch），映射：
   `0x10 SYSTEM_POWER_ON_DONE / 0x11 NX_SET_ID / 0x12 IPCC_INTERFACE_VERSION / 0x13 FIRMWARE_VERSION / 0x14 SYSTEM_INIT_DONE / 0x20 SYSTEM_POWER_OFF_REQUEST / … / 0x83 COMMAND / 0x84 COMMAND_ACK / 0xb0 LOG_START / 0xb1 LOG_STOP / 0xb6 SHELL_COMMAND / 0xe0 ERROR / 0xf0..0xf5 FIRMWARE_UPDATE_*`。
   → 现场 0xB0/0xB1=LOG_START/STOP、0xB6=SHELL_COMMAND（`st cap` 走的正是 SHELL_COMMAND）。**此处"名字是整串存的"**，`GetSubIDName`/`MakeString` 只做"CID 名 + `0x`子号"拼接（`%s:0x%x`）。

**旁证（handler 表，非名字表）**：`libcapture-fw-prod.so` `.data` 中有一批 **R_ARM_ABS32 静态函数指针表**（`raw8/repro/capfw/handler_tables.txt`）。`CNotifyHandler::arpCategoryTbl`（11 项）给出 MCB 业务**类别枚举**：
`0=Empty, 1=System, 2=Still, 3=Liveview, 4=Movie, 5=Lens, 6=QuickPB, 7=Strobe, 8=Shutter, 9=Operation, 10=Production`；
各 `arp*NtfyFnc` 表按子号索引到 handler（如 `arpStillNtfyFnc` 42 项）。⇒ 与 8 个名字域**大体对应但不等价**（Strobe/Shutter 拆开、多出 Operation/Production），说明"域"是命名分组、类别是派发分组。

### 6. 复现步骤

```bash
# 1) WSL rootfs-112 中导出目标 .so
MSYS_NO_PATHCONV=1 wsl -d NXKS2 -u root -- bash -lc \
  'cp /opt/nxks2/rootfs-112/usr/lib/libcapture-fw-prod.so \
      /mnt/d/download/NX-KS2-88/raw8/repro/capfw/'

# 2) 反汇编命令名解析器（VA==off，0x511e0 起 2152B，含字面量池）
arm-linux-gnueabihf-objdump -d --start-address=0x511e0 --stop-address=0x51a48 \
  libcapture-fw-prod.so > ccs_disasm.txt

# 3) 重建 id->name（脚本内解析二分树 + 字面量池）
python3 raw8/repro/parse_ccs.py      # -> raw8/repro/mcb_command_enum_table.txt (68 行)
python3 raw8/repro/dump_tables.py    # .data 表内容
python3 raw8/repro/parse_handler_tables.py  # handler 表(读 capfw/reldump.txt)

# 4) 反证：无 {id,name} 指针表
readelf -rW libcapture-fw-prod.so | grep -cE 'a3[4-9][0-9a-f]{2}'   # => 0
```

**证据文件清单（raw8/repro/capfw/）**：`libcapture-fw-prod.so`、`mcbtest`、`ccs_disasm.txt`(函数+字面量池反汇编)、`enum_ccs_capfw.tsv`(中间结果)、`strings.txt`(节选)、`reldump.txt`(重定位)、`handler_tables.txt`。
脚本：`raw8/repro/{parse_ccs.py, dump_tables.py, parse_handler_tables.py, wsl_probe_*.sh}`。

### 7. 置信度分栏

- **硬证据（可直接引用）**：§1 全部（函数地址/大小、字面量池→串地址公式、串池连续序、0 指针重定位、注册点 0x46b58）、§2 全表 68 条（每条给 leaf/str 双地址）、§5 两个编号空间与 GetCIDName 33 条、handler 类别表。
- **推断**：各域"未列号即不存在"（基于该函数覆盖率）；LENS 0x506–0x508 空洞的非存在性。
- **证伪**：§4 首行"名字非整串存放"。

## 2. st 全表 × MCB 协议 × di-camera-app 全量反编译（2026-10-08）

> 来源：`MCB_PROTOCOL_AND_ST_TABLE_2026-10-08.md`（已归档至 `docs/archive/`）

> 承接 [`ST_RE_AND_REPRO_2026-10-08.md`](../archive/ST_RE_AND_REPRO_2026-10-08.md)（已归档）。本篇 = 三项深化：
> **① di-camera-app 全量反编译 ② st top_cmd_list 全表（运行时真值） ③ MCB 消息协议档案**。
> 原始数据：`raw8/repro/{st_cmd_table_runtime.txt, mcb_command_dict_raw.txt, mcb_exports_raw.txt, mcb_strings.txt, sysv_msg_capture.txt}`
> Ghidra 导出：`E:\ghidra_out\{di-camera-app.arm_dump, libmulticore-bridge.so_dump, libshell-command.so_dump, mcbtest.arm_dump}`

---

### 1. st 命令表（运行时真值 · 28 条全解 ✅）

**方法**：`qemu-arm-static -g` + `gdb-multiarch`，断在 st `main`(0x8810)，**经 st 自身 GOT 槽位(0x10c94)直取 `top_cmd_list` 运行时地址**（绕开 .so 基址难题）。
基址：libshell-command.so = `0x4084C000`；表 @ `0x4086a33c`；`process_option` @ `0x4086a2cc`。

**结构**：每条 8 字节 `{char* name; void* handler}`，"end" 哨兵；**27 条命令全部有 handler**（此前"多数为 0"是文件态误读）。

**分流逻辑**（st main → 查表 → `process_option[i]`）：

| 路径 | 条件 | 行为 |
|---|---|---|
| A 本地执行 | option=0 | `shell_exec(argc, argv, list)` → `handler(argc-1, argv+4)` |
| B 消息派发 | option=1 | `shell_sndmsg(argc, argv, 0x8828)` → **SysV 消息队列** |
| C 未实现 | handler==0 | `"%s : not implement command !!!"`（当前无命令命中此路径） |

- **B 路径 = log / util / app / leak 四条**（其余 23 条走本地）
- 完整表：见 `raw8/repro/st_cmd_table_runtime.txt`（含每条 handler 偏移 ↔ 符号名映射）
- ★ **cap = `shell_capture`(+0x8984)**，内部：`mcb_init → mcb_register_handler(0xB6, ack) → mcb_send(0xB6, 0, len, "st <args>") → 等 ack（10s 超时）→ 失败打印 "Cann't execute capture command over MCB!"`（`shell_capture.cpp:0x130`）——与 qemu 实测输出逐字吻合

**28 命令行为对照（chroot 实跑，全 27 条响应）**：help/readl/writel/dump/gpio/hdmi/lcd/pmu/clk/key/firmware/devman/stlcd/bat/rtc/tbm/micom/misc/oic/dvfs/adc → 各自 usage 正常打印；thread → 启动线程；**log/util/app/leak → 走 SysV（见 §2）**；cap → MCB 报错（>6s 慢失败）。

### 2. SysV 消息体系（key 0x8828）—— ★ 离线拦截成功

- `shell_sndmsg(argc, argv, 0x8828)`：`sem_open(...)` + `msgget(0x8828, IPC_CREAT|0666)`
- **拦截实验**：WSL 宿主侧自写 C 程序 `msgget(34856=0x8828, IPC_CREAT|0666)`（与 chroot 共享内核 IPC 命名空间），chroot 里跑 `st log` → **捕获到完整消息**！
- **捕获内容（208 字节）**：
  ```
  u32: 0x00000002 | "/usr/bin/st\0" | "log\0" | 参数区(补零)
  ```
  ⇒ 消息格式 = `{u32 类型/flags; char exec_path[]; char command[]; args...}`，即**"谁 + 命令 + 参数"的远程执行请求**（相机上由常驻服务端接受执行）。st log 随后阻塞等回执（超时被我们杀掉）。
- 原理解锁：`0x8828` 不是代码地址，是**消息队列 key**（=34856）——此前反编译误读为函数指针实参，已纠正。

### 3. MCB 消息协议档案（libmulticore-bridge.so）

#### 3.1 栈图（全部功能）

```
业务 (shell_capture / mcbtest / …)
  │  mcb_send(u16 cmd, u16 sub, u16 len, u8* data)     ← 公开 C API（26 个 mcb_/Icm_ 导出）
  ▼
CSender::createFrame(cmd, sub, len, data)
  │   堆分配 CFrame(16B 对象)；stFrameHeader 栈构: {u16 cat=2; u8; u16 cmd; u16 sub; u16 len}
  ▼
CFrame::Build(hdr, data)      // 缓冲 = len + 12B 前缀; 拷贝: buf[0..5]←hdr[2..7]; payload@buf+6
  ▼
CSender::send(CFrame)         // 入 CFrameQueue；Category()==0x10 走优先道；满则 "[CSender] Queue is full."
  ▼
Sender 线程 (activate: FThread.Create(0x18ef0, 0x6000) + eventflag)
  ▼
CIPCCDriverIf::Send(buf, size)
  │   校验: size 不得超过 0x18C (396B) — "ERROR: INVALID Size"
  ▼
ipcc_write_pkt(buf, destCpuID(), size)   ← libudd5.so 提供（内联 IPCC UDD 设备层）
  ▼
内核 IPCC 驱动（失败即 "IPCC UDD open is failed!!"，字符串归属 = libudd5.so 实证）
```

#### 3.2 关键事实

- **接收**：IPCC 中断 → `CIPCCDriverIf::OnInterrupt` → 监听回调 → `CReceiver`（`Subscribe(cmd, handler)`）→ 按 **u16 命令号**分派到 `mcb_register_handler(cmd, fn)` 注册者，handler 签名 `int fn(int, u8*)`
- **公开 API**（26 个）：`mcb_init/terminate`、`mcb_send/mcb_raw_send`、`mcb_register_handler/unregister`、`mcb_register_raw_int_handler`、`mcb_lock/unlock/history`、`mcb_get_destAlive`、`mcb_set_ignore_init_checking`、`mcb_debug_set_command_name_function / attribute_name_function`（供调试名解析）
- **互通锁**：`CIntercoreMutex`（Icm_lock/unlock + eventflag）
- **帧最大载荷**：396B/包（0x18C）；帧头 category 常量 = 2（另有 0x10 优先类）
- 源码路径泄漏：`src/sender/Sender.cpp`、`source/shell_capture.cpp`（→ 相机内部分层命名）

#### 3.3 ★ 命令 ID 字典（mcbtest 内 `capture_command_string`，1868B 大 switch）

ID 空间规律：**高字节=域，低字节=命令**；已提取 40+ 条骨架（完整表见 `mcb_command_dict_raw.txt`）：

| 域 | 范围 | 实例 |
|---|---|---|
| SYS_ | 0x1xx | 0x101 CIS_CLEANING / 0x102 CONTROL_STROBE / 0x104 CHANGE_MODE / 0x106 POWER_SAVE / 0x107 POWER_OFF |
| STILL_ | 0x2xx | 0x201 START_CAPTURE / 0x204 LOCK_AEAF / 0x205 LOCK3A / 0x208 TOUCH_AF / 0x20e SET_AEL_BV |
| LV_ | 0x3xx | 0x301 START / 0x302 STOP / 0x304 SCENE_DETECTION / 0x30c ZEBRA_CTRL / 0x30d PANORAMA_DISPLAY |
| MOVIE_ | 0x4xx | 0x401 START_RECORDING / 0x402 STOP_RECORDING / 0x404 TOUCH_AF |
| LENS_ | 0x5xx | 0x501 POWER_ZOOM / 0x502 SET_OIS / 0x505 FW_UPDATE / 0x509 SET_MF_POSITION |
| QUICKPB_ | 0x6xx | 0x601 CHECK_IMAGE / 0x602 PB_REQUEST / 0x604 DELETE_FILE |
| FLASH_ | 0x7xx | 0x701 EXT_HSS_ONOFF / 0x703 MULTI_ONOFF |
| PRD_（生产） | 0xaaXX | CAPTURE_RAW_PP / CAPTURE_RAW_SSIF / WRITE_LOG_TO_NAND / SET_FOCUS_POSITION … |

- 现场观测的活跃命令号：**0xB0**（cap log on）、**0xB1**（cap log off）、**0xB6**（cap 命令行下发）
- `mcbtest`（584KB，1312 函数反编译）= 完整用法参照：注册 0x10/0x21/0x23/0x31/0x80–0x83/0xA2–0xA4/0xB0/0xB1/0xE0/0xF1/0xF5 等 handler

### 4. di-camera-app 全量反编译（完成 ✅）

| 项 | 数值 |
|---|---|
| 函数清单 | **9,258** |
| 字符串 | **29,264** |
| **反编译全文** | **7,073 函数 / 11.6 MB**（`04_decompiled.c`） |
| 位置 | `E:\ghidra_out\di-camera-app.arm_dump\`（另存 400 函数截断版 `_cap400` 作对照） |
| 工具 | `test_server/isp/ghidra/DumpELF.java`（上限参数 20000 全量） |

### 5. 手法速记（新，可复用）

1. **gdb 直取运行时装**：`qemu-arm-static -g <port>` + `gdb-multiarch`（`set sysroot <rootfs>` 本地读符号）→ 断 main → 经**程序自身 GOT 槽位**（`readelf -r` 查）读目标符号运行地址 → 解码数据结构。**绕开 .so 基址/ASLR 一切麻烦**。
2. **SysV 消息拦截**：写 C 用 `msgget(key, IPC_CREAT|0666)` 抢占队列 + `msgrcv` 抓包；chroot 与宿主共享 IPC 命名空间。
3. chroot 复现三件套：`/proc` 需挂、`/dev/shm` 需在（否则 sem_open→sem_init 段错误）、投文件到**存在的**目录。

### 6. 资产与下一步

- 新增：`raw8/repro/` 5 份原始数据；`E:\ghidra_out\*_dump\` 4 套反编译
- 下一步候选：① di-camera-app 全量伪代码上检索（复用 p7 的 ps_scan 模式）② 0xB6/0xB0 完整链路在真机 telnet 侧验证（铁律 90）③ mcbtest 在 chroot 内运行（离线复现 MCB 测试场景，需补 /dev/ipcc 类设备环境）
