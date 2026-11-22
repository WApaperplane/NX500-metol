# 原生菜单与 PW 面板：结构与 ABI（2026-10-15）

> **2026-11-20 重写**：原 `UI_R1A_PW_DATAFLOW`、`UI_R1B_MENU_ABI`、`UI_R1C_APPLY_TRANSACTION`、
> `UI_R1_SUPPLEMENT_MENU_CHAIN`（4 份 739 行）已归档到 `docs/archive/`。
> 对象：原厂 `di-camera-app`（**不是 p7**）。
> **地址体系与 p7 不同**：`file < 0x4384AC ⇒ VA = file + 0x8000`；`≥ ⇒ VA = file + 0x10000`。
> 口径：✅ 实测（反编译/转储/全盘搜索）｜🔶 推断｜❓ 未决。

---

## 0. 结论

| # | 结论 | 强度 |
|---|---|---|
| 1 | **菜单面链条全解**：主菜单 = `CAPPGUIMenuView`（四深度列表，记录步长 **0x24**）+ 顶层 4 页签（CAMERA / MOVIE / CUSTOM / SETTING）+ 子项集（`_INIT_105` 巨型构造器在启动期写入 `.bss`） | ✅ |
| 2 | **「画面向导」原生接线全部就位**：菜单子项 `"CAP_FN_PW"` = `{域 id 0xda, 选项数 14, 选项表 &DAT_0047F858}`，与 OSD、SmartPanel 共用同一域 id 体系 ⇒ **菜单面是原生入口的可行载体** | ✅ |
| 3 | **UI 本体 = `CAPPGUIGadgetPWState`**：5 组列表 + **7 个滑块**（描述表 `DAT_0046e52c`，7 项 × 7 词） | ✅ |
| 4 | **7 维值项 id 已锁定**：`0xDC/0xDD/0xDE` = R/G/B（0..199）、`0xDF` = HUE、`0xE0/0xE1/0xE2` = 三个双向标量（0..20，显示 = 内部 − 10） | ✅ |
| 5 | **最小可复用事务 = `GUI_Set_System_Value(4, 4, value_id, value)`**：7 维各自独立，不依赖 UI 状态机 | ✅ |
| 6 | 文案链：`osd_get_text()`（VA `0xB8344`）= `dcgettext(0, msgid, 5)`；但 rootfs-112 全盘**无 `di-camera-app.mo`**（102 个 `.mo` 全属 WebKit/wrt）⇒ 最终解析位置待真机一条只读命令核实 | ✅/❓ |
| 7 | 与 p7 的接口**跨侧未闭合**：App 经 capfw/MCB 下发，p7 侧接收端属 `PW_APPLY_TRIGGER_CHAIN` 研究域 | ⚪ 边界 |

---

## 1. 菜单记录 ABI（写补丁前必读）

```
记录 = 固定 0x114 字节 × N 条
字段 = [icon VA ptr(4)] [id int(4)] [name VA ptr(4)] [264 B 零填充]
无计数字段、无哨兵 —— 数量由消费点常量或调用参数给出（PW 域 = 14）
```

- 两份副本**不是同一张表的两个拷贝**：
  - 表① file `0x40C6A8` = **OSD 面**（`Draw_pw` 直接读 `.rodata`）；
  - 表② file `0x41754C` = **Smart Panel 面**（`GetButtonInfo` 装载到 `.bss` 工作副本 `0x596EE4`）。
  - 旧表述「另一 UI 面副本」由此修正。
- 字符串（icon 路径 / 内部名 / MSG 名）均为**静态 `.rodata`**，无动态分配 ⇒ 文本生命周期 = 进程级常驻，无释放问题。
- 线程：**UI 线程内调用** 🔶（函数均为视图绘制/更新族；静态中未取得线程绑定证据 ❓）。

### 写入规则（供补丁直接引用）

1. **不改 `id` 字段**（域 id 体系被 OSD / SmartPanel / 菜单三面共用）；
2. 字符串**等长替换**（VA 指向 `.rodata`，长度变化会越界）；
3. `name` 目标必须落在解析链内（见 §0 第 6 条的 `.mo` 未决项）。

---

## 2. 7 维值项与事务

| 值项 | id | 值域 | 备注 |
|---|---|---|---|
| R / G / B | `0xDC` / `0xDD` / `0xDE` | 0..199 | 与 p7 色彩编码 `v*0xff/200` 吻合 |
| HUE | `0xDF` | 0..20 | 显示值 = 内部值 − 10（±10） |
| 其余三个双向标量 | `0xE0` / `0xE1` / `0xE2` | 0..20 | 同上 |

**调节事务**：`GUI_Set_System_Value(4, 4, value_id, value)`；
触摸变体 `GUI_Touch_Set_System_Value` 会先置 `0x7f7` 触摸标志。

**应用下发链**（拍摄启动路径）：

```
__ui_start_capture
  → UI_Operate_Start_Capture(8)
    → __ui_set_pwb_capture_param     （读掩码 0x100 → 3 个位号 → 0xF00xx 编码 → 暂存）
      → UI_Mm_Camera_Start_Capture()
        → mm_camera_capture_still(18 参数)   ← capfw 出口
```

⇒ **「调节」和「应用」是两条独立事务**：前者只写值系统，后者在拍摄启动链上取掩码并编码。
补丁若只改值不走到拍摄链，画面不会变 —— 这是 A2 研究里最容易踩的坑。

---

## 3. 「CUSTOM1 之谜」（未闭环）

选项表里三个 CUSTOM 条的 **MSG 字段全指向 `MSG_MENU_PW_CUSTOM03`**（共享占位），
**代码中零直接改写**。运行时显示名的来源收敛为「gettext（`.mo`）解析 + 可能的动态槽名机制」，
需 `.mo` 核实后闭环（§0 第 6 条）。

---

## 4. 待上机核实（只读）

1. rootfs 里到底有没有 PW 相关 `.mo`，`dcgettext(0, msgid, 5)` 的实际解析结果是什么。
2. `CUSTOM1..3` 的显示名是否随槽位变化（若变，则存在动态槽名机制）。

---

## 5. 原文

`docs/archive/UI_R1_SUPPLEMENT_MENU_CHAIN_2026-10-15.md`（菜单构造全链、`_INIT_105` 展开）、
`docs/archive/UI_R1A_PW_DATAFLOW_2026-10-15.md`（数据流与两份描述表、刷新入口）、
`docs/archive/UI_R1B_MENU_ABI_2026-10-15.md`（ABI 逐字段证据、两份副本差异）、
`docs/archive/UI_R1C_APPLY_TRANSACTION_2026-10-15.md`（5 组 × 7 滑块结构、事务逐调用点）。
