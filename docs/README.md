# Metol 文档索引（旧称NX-WA）

> 更新：**2026-11-07**（视频 LUT 随 apply 联动 + 2.5K 片门全开；10bit / 内置 gamma 判死）· 分支：`nxwa`
> 本目录按**时效**分区，避免"旧结论淹没新结论"。

## 目录结构

```
docs/
├── README.md        ← 本文件（索引）
├── current/         ← 现行权威：据此做决策
├── archive/         ← 历史归档：结论可能已被推翻
└── evidence/        ← 逆向证据片段（可复现的原始数据/脚本）
```

---

## 1. 先读这四份

| 文档 | 定位 |
|---|---|
| [`HANDOVER_2026-10-16.md`](current/HANDOVER_2026-10-16.md) | ★★★★★ **总入口**：约定、死路、门控、执行序 |
| [`P0_EXECUTION_PLAN_2026-11-20.md`](current/P0_EXECUTION_PLAN_2026-11-20.md) | ★★★★★ **当前执行计划**：录像并发门控 + 成片完整性 + 收尾状态机（唯一「毁数据」风险面） |
| [`ONDEVICE_SOP_2026-10-16.md`](current/ONDEVICE_SOP_2026-10-16.md) | ★★★★★ **上机日 SOP**：就位 / 判据 / 回退 / 证据落点（§10 LUT 部分已被取代，见文首横幅） |
| [`../README.md`](../README.md) | 对外介绍、安装、功能（★ 用户视角从这里进） |

---

## 2. 现行结论（10-15 ~ 11-20）

### 2.0 最新一轮（11-08 ~ 11-20）★ 先看这一组

| 文档 | 定位 |
|---|---|
| [`P0_EXECUTION_PLAN_2026-11-20.md`](current/P0_EXECUTION_PLAN_2026-11-20.md) | ★★★★★ **录像并发门控 + 成片完整性 + 收尾状态机**：唯一的「毁数据 / 拔电池」风险面；PC 侧 19/19、真机两轮守卫 253/253 |
| [`VLOG_BLACK_SCREEN_2026-11-20.md`](current/VLOG_BLACK_SCREEN_2026-11-20.md) · [`UI_SINGLE_ENTRY_2B_2026-11-20.md`](current/UI_SINGLE_ENTRY_2B_2026-11-20.md) · [`DYN24_MAP_2026-11-20.md`](current/DYN24_MAP_2026-11-20.md) | ★★★★ V-Log 录像黑屏根因（DYN24 整表重指 A18）、EV+WiFi 单入口 2b 定案、DYN24 映射 |
| [`LUMIX_VLOG_IMPORT_2026-11-20.md`](current/LUMIX_VLOG_IMPORT_2026-11-20.md) · [`UI_LUT_SCOPE_2026-11-20.md`](current/UI_LUT_SCOPE_2026-11-20.md) | ★★★ Lumix V-Log 库导入（烘成 Standard 输入）、UI 域范围 |
| [`WIFI_5GHZ_FINAL_2026-11-20.md`](current/WIFI_5GHZ_FINAL_2026-11-20.md) | ★★★★ **5GHz 线收束**：单点 `dev_cap_mask`、固件 5G 表、FCC 对照、救砖缺 `dnloader.bin`；**决定不发射** |
| [`RUNTIME_ARCHITECTURE_DESIGN_2026-11-16.md`](current/RUNTIME_ARCH_AND_P0_2026-11-16.md) · [`RUNTIME_P0_ACCEPTANCE_2026-11-16.md`](current/RUNTIME_ARCH_AND_P0_2026-11-16.md) · [`KERNEL_PRIVATE_DEVICES_2026-11-16.md`](current/KERNEL_PRIVATE_DEVICES_2026-11-16.md) | ★★★★ 运行态架构与 P0 验收；内核私有设备清单（勿碰项） |
| [`FILMLAB_SLOT_AND_BITRATE_2026-11-14.md`](current/FILMLAB_SLOT_AND_BITRATE_2026-11-14.md) | ★★★★ FilmLab 槽位与码率 |
| [`NX1_NX500_MEMORY_2026-11-10.md`](current/NX1_NX500_MEMORY_2026-11-10.md) · [`EXT_REVIEW_NX500_SRW_2026-11-10.md`](current/EXT_REVIEW_NX500_SRW_2026-11-10.md) · [`NXUI_ENABLE_GATE_2026-11-10.md`](current/NXUI_ENABLE_GATE_2026-11-10.md) | ★★★ NX1/NX500 内存对照；外部 6.5K 超采方案复核（**研究态，不合入**）；NXUI 启用门 |
| [`AF_ADDRESS_AND_AE_2026-11-17.md`](current/AF_ADDRESS_AND_AE_2026-11-17.md) · [`AF_MOTION_AND_LOWLIGHT_2026-11-17.md`](current/AF_MOTION_AND_LOWLIGHT_2026-11-17.md) · [`AF_TRIGGER_AND_PATCH_2026-11-17.md`](current/AF_TRIGGER_AND_PATCH_2026-11-17.md) | ★★★★ **AF 二期（6 → 3 份）**：地址体系与 AE 耦合（LV+ISO 只调滤波器，不改相位许可）／低光·预测阶数·主脸打分／SLOW-AF 触发与 `ng/ok/small` 三初值 |
| [`CURVE_CALLLINK_2026-11-17.md`](current/CURVE_ANATOMY_2026-11-17.md) · [`CURVE_DDR_LAYOUT_2026-11-17.md`](current/CURVE_ANATOMY_2026-11-17.md) · [`CURVE_INTERP_2026-11-17.md`](current/CURVE_ANATOMY_2026-11-17.md) | ★★★ 曲线调用链、DDR 布局、插值 |
| [`LUT_INSTANT_SWITCH_2026-11-08.md`](current/LUT_SWAP_MECHANISM_2026-10-16_to_11-08.md) · [`TC_CURVE_ONDEVICE_2026-11-08.md`](current/TC_CURVE_AND_RESTORE_2026-11-08.md) · [`TC_RESTORATION_WORKFLOW_2026-11-08.md`](current/TC_CURVE_AND_RESTORE_2026-11-08.md) | ★★★ 瞬间换表；TC 曲线 19 节点表（**菜单入口已移除，`tc.sh` 留机备份**，见 `ce00885`） |

### 2.0b 上一轮（10-25 ~ 11-07）

| 文档 | 定位 |
|---|---|
| [`VIDEO_LUT_FIRSTFRAME_2026-11-07.md`](current/VIDEO_LUT_AND_10BIT_2026-11-07.md) | ★★★★★ **开录首帧就带 LUT**：p7 的 23 项档位指针表（VA `0x80383A28`）全指 A18；预置单槽为何失败；0334/0335 对照证据 |
| [`VIDEO_LUT_AND_2K_2026-11-07.md`](current/VIDEO_LUT_AND_10BIT_2026-11-07.md) | ★★★★★ **视频 LUT 自动联动 + 2.5K 实测**：完成中断加载协议、`nx_lutload` watch 模式、真实 MP4 写句柄识别、灰帧缺陷、FOV 配准 |
| [`VIDEO_10BIT_FEASIBILITY_2026-11-07.md`](current/VIDEO_LUT_AND_10BIT_2026-11-07.md) | ★★★★ **10bit 不可行的逐项证据**：`hvcC` 8bit/4:2:0、全固件 Main10 零命中 ⇒ log 不可达 |
| [`GAMMA_LOG_VERDICT_2026-11-07.md`](current/GAMMA_LOG_VERDICT_2026-11-07.md) | ★★★★ **内置 gamma/log 判死**：`IQ[180]` 恒 NORMAL，app 侧 393 条 MOVIE 字符串零 gamma |
| [`LUT_INPUT_CALIBRATION_2026-11-07.md`](current/LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md) | ★★★★ **输入曲线怎么测**：3DLUT 在 JPEG 编码器之前 ⇒ 拍 JPG 即测输入端；`measure_tone.py` 反解 gamma/肩部/趾部 |
| [`RES25K_TOGGLE_2026-11-07.md`](current/RES25K_TOGGLE_2026-11-07.md) | ★★★ **原版 2.5K 映射的核对**（功能属上游 NX-KS，非本项目新增）：视角实测 + 菜单纠错（原判断「取消勾选无恢复动作」已撤回） |
| [`LUT_LIBRARY_AND_MIX_2026-11-01.md`](current/LUT_MIX_AND_LIBRARY_OPS_2026-10-31.md) | ★★★★★ **机内混合 + 库管理 + 面板三态**：`lutmix.arm` 契约与逐字节验证；`order`/`_trash` 语义；`recipelib.sh` 块级手术与双校验；BROWSE/EDIT/MIX 与键位表；含 `nk_spacing` 撑行坑 |
| [`LUT_ONDEVICE_PIPELINE_2026-11-01.md`](current/LUT_PIPELINE_ROADMAP_2026-10-09_to_11-01.md) | ★★★★ 机内 LUT 链路现状：三条转换路径与格式契约；缺口只剩「文件怎么进卡」（FTP 即可） |
| [`LUT_STRENGTH_MIX_2026-10-31.md`](current/LUT_MIX_AND_LIBRARY_OPS_2026-10-31.md) | ★★★★ 强度混合的数学：逐节点插值 = 输出域混合；可辨粒度；三个必须先定的语义 |
| [`DUAL_LUT_FEASIBILITY_2026-10-31.md`](current/DUAL_LUT_AND_MIX_2026-10-31.md) | ★★★ 两张表串联不可行的证明（混合是它的替代答案） |
| [`JPEG_QUALITY_CONSTANTS_2026-10-31.md`](current/JPEG_QUALITY_CONSTANTS_2026-10-31.md) | ★★★★ 质量不是标量：17 档预置量化表、`FUN_80128378` 查表、4:2:2 为硬件固定 |
| [`LUT_GRID_17_VS_33_2026-10-31.md`](current/LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md) | ★★★ 17³ native 与 33³ 设计的取舍实测 |
| [`CAPTURE_CHAIN_HARDENING_2026-10-31.md`](current/CAPTURE_CHAIN_HARDENING_2026-10-31.md) · [`MULTIEXP_P7_PATH_2026-10-31.md`](current/MULTIEXP_P7_PATH_2026-10-31.md) | 拍摄链加固与多重曝光 p7 路径 |
| [`CUBE2LUT_NATIVE_FIX_2026-10-25.md`](current/CUBE2LUT_CONVERTER_2026-10-25.md) | ★★★★★ **机内转换器语义修正**：8880 B 旧版是 RGB 直通；现行 10,828 B `b798964d` 必须核对 |
| [`LUT_WEB_SOURCE_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) · [`LUT_PHYSICAL_FILM_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) | 外部 CLUT 导入的解码坑；物理胶片配方线 |
| [`LUT_AUDIT_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) · [`LUT_STYLE_AUDIT_V2_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) · [`LUT_DEDUP_AND_GAPFILL_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) · [`LUT_BRAND_LOOKS_2026-10-25.md`](current/LUT_LIBRARY_2026-10-25.md) | 库体检、风格复核、去重补隙、品牌色 |
| [`LUT_CRCBY_ONDEVICE_FIX_2026-10-25.md`](current/LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md) · [`LUT_COLORSPACE_REGRESSION_REVIEW_2026-10-25.md`](current/LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md) · [`COLOR_DEF_2026-10-25.md`](current/COLOR_DEF_2026-10-25.md) | CrCbY 机内修正、色彩回归复核、`colordef` 常量 |
| [`NX500_AF_STATIC_OVERVIEW_2026-10-31.md`](current/NX500_AF_STATIC_OVERVIEW_2026-10-31.md) | ★★★★★ **AF 静态分析九轮总纲（9 → 1 份）**：两套命名空间不可混（`0xNNNNN`=CAFData / 槽位=CIQ_SysDataIF）、主体保持已有实现、眼部 AF 是纯缺口、逐偏移扫描对 `SetParameter` 无效 |

### 2.1 LUT：运行态换表（★ 当前主线）

> 10-25 ~ 11-08 的 LUT 记录已按机制合并为 5 份（见 §6.5），每份开头有「关键结论」表与「机制演进」节。

| 文档 | 定位 |
|---|---|
| [`LUT_SWAP_MECHANISM_2026-10-16_to_11-08`](current/LUT_SWAP_MECHANISM_2026-10-16_to_11-08.md) | ★★★★★ **换表机制全史（11 份合并）**：E4「成片死路」→ 打错表（A17 vs A12）→ `SelLUT=bits[5:4]` → 4 字节+写槽+半按 → 退出还原 / 改错 `View4[3]` → 秒切免半按 |
| [`STILL_PATH_AUDIT_2026-10-20`](current/STILL_PATH_AUDIT_2026-10-20.md) | ★★★★★ **成片无 LUT 的根因**：Still6 / BV12 两条互斥分支，屏幕色彩开启走 BV12；含 `still_probe.sh` 只读判别 |
| [`ADOBERGB_VIEW_PATH_2026-10-20`](current/ADOBERGB_VIEW_PATH_2026-10-20.md) | ★★★★ AdobeRGB 取景不生效：View4 四项与「色彩空间 × 屏幕色彩」的映射；`view-set` 一次写全四项 |
| [`LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07`](current/LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md) | ★★★★★ **表格式与输入端（6 份合并）**：链路坐标是 CrCbY、17³ native、`measure_tone.py` 反解输入曲线 |
| [`LUT_LIBRARY_2026-10-25`](current/LUT_LIBRARY_2026-10-25.md) | ★★★★ **库（6 份合并）**：体检 / 风格复核 / 去重补隙 / 品牌色 / 外部 CLUT 的 `512²=64³` 解码坑 |
| [`LUT_MIX_AND_LIBRARY_OPS_2026-10-31`](current/LUT_MIX_AND_LIBRARY_OPS_2026-10-31.md) | ★★★★ **混合与库操作（2 份合并）**：逐节点插值 ≡ 输出域混合；`lutmix.arm` 八动词 + 三态面板 |
| [`LUT_PIPELINE_ROADMAP_2026-10-09_to_11-01`](current/LUT_PIPELINE_ROADMAP_2026-10-09_to_11-01.md) | ★★★ **管线（3 份合并）**：三路径设计 → 复核 → 「转换与铺设已全在机内」 |
| [`NXUI_NUKLEAR_2026-10-17`](current/NXUI_NUKLEAR_2026-10-17.md) · [`NXUI_FOCUS_HANDOFF_2026-10-18`](current/NXUI_FOCUS_HANDOFF_2026-10-18.md) · [`UI_FRAMEWORK_ALTERNATIVES_2026-10-17`](current/UI_FRAMEWORK_ALTERNATIVES_2026-10-17.md) | ★★★★ NXUI 安装与验证、X11 焦点不归还缺陷、换 UI 框架的可行性分层 |

### 2.2 上机、交付与设计（10-15 ~ 10-16）

| 文档 | 定位 |
|---|---|
| [`DELIVERY_RESUME_AUDIT_2026-10-16.md`](archive/DELIVERY_RESUME_AUDIT_2026-10-16.md) | P5 投递失败的恢复链审计（LUT 后端部分已退役，P5 分析仍有效） |
| [`ONDEVICE_NATIVE_LABEL_RESULT_2026-10-16.md`](current/ONDEVICE_NATIVE_LABEL_RESULT_2026-10-16.md) | 原生标签真机 POC（11 字节） |
| [`T2_PRESET_PROTOCOL_2026-10-16.md`](current/T2_DELIVERY_AND_PRESET_2026-10-16.md) | `nxwa.preset/1` 协议设计（**未实施**） |
| [`M2_RECIPE_NAME_DESIGN_2026-10-16.md`](current/M2_RECIPE_NAME_DESIGN_2026-10-16.md) | 配方名 `.mo` 文案层设计（**未落地**） |
| [`T2_DELIVERY_REVISION_2026-10-15.md`](current/T2_DELIVERY_AND_PRESET_2026-10-16.md) | 投递链修订（去 restart，只就位） |
| [`MEM_INJECTION_EVAL_2026-10-15.md`](current/MEM_INJECTION_EVAL_2026-10-15.md) | 内存注入路线评估（poker 退出主候选） |
| [`A2_READBACK_VERDICT_2026-10-15.md`](current/A2_PATCH_AND_READBACK_2026-10-14.md) | A2 刷机状态逐字节裁决 |

---

## 3. 技术基线（不随版本变，可长期引用）

### 3.1 p7 / 3D LUT

| 文档 | 定位 |
|---|---|
| [`3DLUT_TABLE_FORMAT_2026-10-08.md`](current/3DLUT_PATHS_FORMAT_ISOLATION_2026-10-08.md) | ★★★★★ **表格式**：`4913×4B`、索引、槽步长 `0x4D00`、槽池布局 |
| [`3DLUT_APPLY_PATHS_2026-10-08.md`](current/3DLUT_PATHS_FORMAT_ISOLATION_2026-10-08.md) | ★★★★★ **五条应用线路** + 寄存器写序（`+0x00c`/`+0x010`/`+0x008`） |
| [`3DLUT_CUBE_IMPORT_TOOL.md`](current/3DLUT_CUBE_IMPORT_TOOL.md) | `.cube` 导入工具 `nx3dlut.py` 用法 |
| [`3DLUT_API_GUIDE.md`](current/3DLUT_API_GUIDE.md) · [EN](current/3DLUT_API_GUIDE_EN.md) | LUT API 参考（**官方 API 路线，非当前主线**，文首有横幅） |
| [`P7_3DLUT_PATHS_2026-10-07.md`](current/P7_3DLUT_PATHS_2026-10-07.md) | 三条 C++ 路径全解（RTTI + vtable） |
| [`P7_DISPATCH_AND_LSC_2026-10-07.md`](current/P7_DISPATCH_AND_LSC_2026-10-07.md) | 跳转表机制纠正 + 镜头阴影描述符 |
| [`P7_EFS_MODULE_2026-10-08.md`](current/P7_EFS_MODULE_2026-10-08.md) | EFS 模块逆向（`nand_write` 实为 MMIO） |
| [`P4_HW_WINDOW_CONFIRMED_2026-10-11.md`](current/P4_HW_WINDOW_CONFIRMED_2026-10-11.md) | 3DLUT 硬件窗口两态之谜结案 |

### 3.2 PW / 属性总线

| 文档 | 定位 |
|---|---|
| [`PW_ID_MAP_FULL_2026-10-10.md`](current/PW_ID_MAP_FULL_2026-10-10.md) | ★★★★★ **PW id 全图** + 为什么外部够不到 R/G/B/HUE |
| [`PW_PARAM_CHANNEL_2026-10-08.md`](current/PW_PARAM_CHANNEL_2026-10-08.md) | ★★★★★ 三通道上机定论（只有"参数"通道有效） |
| [`PW_APPLY_TRIGGER_CHAIN_2026-10-10.md`](current/PW_APPLY_TRIGGER_CHAIN_2026-10-10.md) | 「画面向导→自定义1」p7 侧触发链 |
| [`ATTR_BUS_MCB_2026-10-09.md`](current/ATTR_BUS_MCB_2026-10-09.md) | ★★★★★ 属性总线传输层全解 |
| [`MCB_PROTOCOL_AND_ST_TABLE_2026-10-08.md`](current/MCB_PROTOCOL_AND_ENUM_2026-10-08.md) | MCB 协议档案 + `st` 全表 |
| [`MCB_COMMAND_ENUM_TABLE_2026-10-08.md`](current/MCB_PROTOCOL_AND_ENUM_2026-10-08.md) | MCB 命令号枚举全表 |
| [`PW_ONECLICK_DESIGN_2026-10-09.md`](current/PW_ONECLICK_DESIGN_2026-10-09.md) | 一键滤镜设计（PW 直推版） |

### 3.3 系统 / 刷机 / App

| 文档 | 定位 |
|---|---|
| [`NX500_ARCHITECTURE.md`](current/NX500_ARCHITECTURE.md) | ★★★★★ 系统与软件架构（实机抓取） |
| [`SLP_CONTAINER_FORMAT_2026-10-08.md`](current/SLP_CONTAINER_FORMAT_2026-10-08.md) | ★★★★★ SLP 容器格式全解（JAMCRC，无签名） |
| [`FW_UPGRADE_VERIFY_CHAIN_2026-10-08.md`](current/FW_UPGRADE_VERIFY_CHAIN_2026-10-08.md) | 固件升级校验链 |
| [`ILAUNCHER_FIRMWARE_FLASH_2026-10-07.md`](current/ILAUNCHER_FIRMWARE_FLASH_2026-10-07.md) | iLauncher 刷机机制（不直刷相机） |
| [`CAMERA_APP_STRUCTURE_2026-10-07.md`](current/CAMERA_APP_STRUCTURE_2026-10-07.md) | `di-camera-app` 结构盘查 |
| [`CAMERA_MENU_ARCHITECTURE_2026-10-07.md`](current/CAMERA_MENU_ARCHITECTURE_2026-10-07.md) | ★★★★★ 菜单渲染档案 + **证明 p7 无 UI** |
| [`DICAM_PATCH_INSTALL_PATHS_2026-10-12.md`](current/DICAM_PATCH_INSTALL_PATHS_2026-10-12.md) | App 补丁安装路径与 Release 布局 |
| [`SHELL_TO_APP_ISOMORPHISM_2026-10-12.md`](current/SHELL_TO_APP_ISOMORPHISM_2026-10-12.md) | shell → App 官方通道替换 |
| [`UI_MENU_AND_PW_ABI_2026-10-15.md`](current/UI_MENU_AND_PW_ABI_2026-10-15.md) | ★★★★ **原生菜单与 PW 面板（4 → 1 份）**：0x114 记录 ABI（无计数/哨兵）、7 维值项 id（R/G/B 0..199）、调节与应用是两条独立事务、`.mo` 文案未决 |
| [`UI_R0_TARGET_INVENTORY_2026-10-15.md`](archive/UI_R0_TARGET_INVENTORY_2026-10-15.md) | 原版 App 样本与哈希台账 |

### 3.4 方法与纪律

| 文档 | 定位 |
|---|---|
| [`GATE_FLOW_2026-10-08.md`](current/GATE_FLOW_2026-10-08.md) | 门控任务流（三态 PASS/FAIL/BLOCKED + 证据契约） |
| [`ERROR_CORRECTIONS_2026-10-07.md`](current/ERROR_CORRECTIONS_2026-10-07.md) | 历史纠错 C1–C9 |
| [`PATCH_SURFACE_REVIEW_2026-10-12.md`](current/PATCH_SURFACE_REVIEW_2026-10-12.md) | 补丁化空间评估（L0–L5 分层 + 判据 J1–J5） |
| [`MOD_DECISION_P7_APP_2026-10-12.md`](current/MOD_DECISION_P7_APP_2026-10-12.md) | p7 与 App 修改决策记录（ADR） |
| [`FIRMWARE_FEATURE_SPACE_2026-10-08.md`](current/FIRMWARE_FEATURE_SPACE_2026-10-08.md) | 固件改写功能空间 + 架构天花板 |

> 其余 10-07 ~ 10-14 的逆向记录仍在 `current/`（未归档），多为过程记录，**不作判据来源**；
> 需要时按文件名在目录里找，或到 `archive/` 查更早的。

---

## 4. 已作废结论速查（看到这些就当没看见）

| 旧结论 | 现状 | 依据 |
|---|---|---|
| View 与 Still 共用池 ⇒ **软件层无法隔离** | ❌ 可隔离：池共用，但**三张登记表是分开的** | `LUT_RETARGET_LIVE_2026-10-18` |
| **成片直灌 LUT 是死路** | ❌ 只有 **LUT1 直灌**无效；改 `Still6` 成片照样换表 | 同上 |
| **LUT1 + `lutsentinel` 守护**是预览主线 | ⛔ 已退役（抢时序 / 只覆盖预览 / 不抗抢回） | 同上 |
| 实机装载源是 **A12 标准槽** | ❌ 是 **A17 `0x81115200`**（肤色表） | 同上 |
| `bit12` 是 SelLUT 选择位 | ❌ **`bits[5:4]`** 才是；`0x100` 与 `0x1100` 都是 SelLUT=0 | `LUT_REGTAB_READ_2026-10-18` |
| 出厂有 **identity / 中性**表可作基准 | ❌ 全是风格表 | `U6_ONMACHINE_RESULT_2026-10-09` |
| `lutpick.sh off` = 关闭 LUT | ❌ 是加载 identity ≠ 关闭；关闭走 `factory_restore` / `lut.sh off` | `LUT_UI_2026-10-18` |
| **`mod_gui`** 是 mod 主界面 | ❌ 已被 **NXUI（Nuklear）** 取代 | `NXUI_NUKLEAR_2026-10-17` |
| 配方 **95 条** | ❌ 现 **68 条**，权威源 `scripts/filmlab/recipes.json` | 仓库 |
| **原生 UI（EFL）** 是方向 | ⛔ 全屏自绘实测是墙；小面板（NXUI）可行 | `UI_FRAMEWORK_ALTERNATIVES_2026-10-17` |
| 改 unit / 依赖可开机重读 | ⛔ 本机是用户态恢复机型，systemd 不重读 unit（P5 死路） | `HANDOVER_2026-10-16` |
| `setvar` 可写 PW 变量 | ⛔ 禁区：id 运行时注册，盲扫曾写死 capture 服务 | `ERROR_CORRECTIONS_2026-10-07` |
| **5G 功率表没随机型发布** | ❌ 固件内建完整 5G 表（39 条 × 17 B）；主机侧 `txpwrlimit_cfg.bin` 只是覆盖文件 | `WIFI_5GHZ_FINAL_2026-11-20` |
| **「四层一致裁剪」**（mask+功率表+FCC+前端） | ❌ 只剩两层：驱动 `dev_cap_mask` + FCC 只报 2.4G | 同上 |
| **NX30 同腔体 ⇒ 可与 NX500 芯片级类比** | ❌ NX30 是 Atheros AR6003（DRIMeIV），NX500 是 Marvell SD8887（DRIMeVs）；只剩「同腔体装过双频天线」 | 同上 |
| **`band` 实验能验证射频通路** | ❌ 只证驱动注册；`fw_bands` 已被 mask 卡死，结果几乎确定 | 同上 |

---

## 5. 本次清理做了什么（2026-10-18）

- **归档 28 份**：旧交接（10-07 / 10-11 / 10-12）、旧任务流（10-09 / 10-10 / 10-12）、
  一次性离机与 QEMU 执行记录、被 NXUI 取代的原生 UI 设计（10-07 / 10-11 / 10-13 / 10-15）、
  被 68 条取代的配方批次记录、10-07 深度调查（中英）。
  → `docs/archive/`，正文未改，保持历史原样。
- **加横幅 24 份**：结论已被取代但仍需可查的技术文档（T1/T3/T4/T5/T6、`3DLUT_API_GUIDE`、
  `P7_RECLAIM`、`U6`、`LUT_*_2026-10-17/18`、`MODGUI_*`、`FEATURE_MATRIX_*` 等），
  文首统一为「⚠️ 2026-10-18 文档清理：本文结论已被取代」+ 指向现行文档。
- **重写本索引**：从 122 条平铺改为「入口 → 现行 → 基线 → 作废速查」四段。
- **同步根 README**：3D LUT 一节按 10-18 结论重写（中英双语）。

> 归档文件里的相对链接（指向 `current/` 的）会失效 —— 需要时用文件名在本目录 `current/` 里找。

---

## 6. 第二轮清理（2026-11-20）

审计工具：`tools/doc_audit.py`（索引断链 / 孤儿 / 同主题）、`tools/doc_pathcheck.py`（文档内路径失效）。

| 项 | 结果 |
|---|---|
| 索引断链 | **0**（§1~§3 的 81 条链接全部有效） |
| 10-18 那批横幅 | **24 份全部在位**，无被后续编辑冲掉 |
| `current/` 规模 | 170 份，其中 **22 份未被索引**（12 份是 11-16~11-20 新增，10 份是 10 月过程记录） |
| 同主题多版本 | 无（`AF_STATIC_ROUND2..9` 是递进轮次，各有独立结论，不是版本覆盖） |
| 文档内失效路径 | 44 份文档、98 处（多为已移动的产物路径，不影响结论） |

**处理**：

- **新增 1 份横幅**：`WIFI_5GHZ_ANTENNA_CASE_2026-11-20.md` —— 其中「NX30 同腔体」的芯片级类比已被同日
  `WIFI_5GHZ_FINAL_2026-11-20.md` 修正（NX30 实为 Atheros AR6003）。
- **补 §2.0 索引**：11-08 ~ 11-20 共 25 份文档进入现行结论区。
- **新增 §4 四条作废结论**：5G 功率表「没发布」、四层裁剪、NX30 芯片类比、`band` 能验射频。
- **未归档任何文档**：本项目「过时」指**结论被取代**，10-18 已定规范为「加横幅而非删除」；
  10 月那批过程记录在 §2.2 已声明「不作判据来源」，且含死路记录（如 G5-3 的「路径 B 不成立」），
  仍有取证价值。

> 若将来要压缩 `current/` 体积，安全的候选是 §2.2 已声明的 10-07 ~ 10-14 过程记录，
> 归档而非删除 —— 但需先确认这些文件未被 `evidence/` 或脚本按路径引用。

### 6.1 本轮实际归档（8 份 → `archive/`）

`U1_ONMACHINE_2026-10-12`、`U4_ONMACHINE_RESULT_2026-10-11`、`U5_ROLLBACK_DRILL_2026-10-11`、
`FLAB_DATAFLOW_DESIGN_2026-10-09`、`FILMLAB_WHEEL_PREVIEW_FEASIBILITY_2026-10-11`、
`UNKNOWN_STRUCTURES_AND_PROJECT_FORK_PLAN_2026-10-08`、`P7_CODE_MAP_COVERAGE_2026-10-08`、
`FW_NATIVE_UI_ONEKEY_FEASIBILITY_2026-10-10`。

筛选标准：日期 10-07~10-14 **且** 未被本索引引用 **且** 不带 10-18 横幅（带横幅者按规范留 `current/`）
**且** 不含被脚本 / `fw/` / 根 README 按路径引用的内容。
一次性执行记录与被实现取代的设计 → 归档；补丁包（A1/A2）、逆向证据（REA_P7）、死路记录（G5-3、P8）、
能力探针（W6）→ 保留。`current/` 170 → 162 份。

### 6.2 断链修复（5 处）

因 10-18 与本轮归档而指向旧位置的引用，已改为指向 `archive/`：
`CAMERA_APP_STRUCTURE`、`CAMERA_MENU_ARCHITECTURE`（→ `NATIVE_UI_DESIGN_2026-10-07`）、
`FILMLAB_LUT_NATIVE_UI_GOALS`、`MEM_INJECTION_EVAL`（→ `NATIVE_UI_ROADMAP_2026-10-15`）、
`MCB_PROTOCOL_AND_ST_TABLE`（→ `ST_RE_AND_REPRO_2026-10-08`）。

> `current/` + `README` 层的相对链接现已 **0 断链**；`archive/` 内部仍有 18 处互相引用失效，
> 按 §5 约定不修（用文件名在本目录 `current/` 里找）。

### 6.3 一个说明：文档里的 `raw8/`、`looks/*.bin` 为什么点不开

审计发现 45 份入口文档引用 `raw8/…`、`rea-evidence/…`、`looks/*.bin`、`test_server/*/产物` 等路径，
这些是**本地工作目录**（逆向中间产物、相机取回的二进制），按仓库策略不入库（`raw8/` 只保留
`emmcbak/` 下的分区备份）。它们记录的是取证落点，不是失效链接，**不要为了"点得开"去删这些路径**。

### 6.4 同主题合并（12 组，30 份 → 12 份）

同一件事被切成多份的，合并为一份，原文件全部归档（`docs/archive/`），正文未删改、标题层级下调一级。

| 合并后 | 由哪些组成 |
|---|---|
| `A2_PATCH_AND_READBACK_2026-10-14` | A2 设计 / 槽写前置 / 回读判定 |
| `MCB_PROTOCOL_AND_ENUM_2026-10-08` | 命令枚举表 / 协议与 ST 表 |
| `TC_CURVE_AND_RESTORE_2026-11-08` | 19 节点表上机 / 还原工作流 |
| `DUAL_LUT_AND_MIX_2026-10-31` | 双表可行性 / 强度混合 |
| `CUBE2LUT_CONVERTER_2026-10-25` | 转换器 / 原生修正 |
| `RUNTIME_ARCH_AND_P0_2026-11-16` | 运行态架构 / P0 验收 |
| `T2_DELIVERY_AND_PRESET_2026-10-16` | 交付修订 / 预置协议 |
| `FEATURE_MATRIX_FULL_2026-10-11` | 功能矩阵 / mod 全量版（两份均带 10-18 横幅） |
| `VIDEO_LUT_AND_10BIT_2026-11-07` | 首帧 LUT / 2.5K / 10bit 可行性 |
| `CURVE_ANATOMY_2026-11-17` | 调用链 / DDR 布局 / 插值 |
| `3DLUT_PATHS_FORMAT_ISOLATION_2026-10-08` | 生效路径 / 表格式 / View-Still 隔离 |
| `NXUI_UI_DETAILS_2026-10-19` | 首帧 / 启动 / 面板 / 滚动 |

`current/` 162 → **144** 份，`archive/` 69 → **99**。合并后全库相对链接 **0 断链**。

> 三组较大的已在 §6.6 处理（AF 九轮、AF 二期、UI_R1）。

### 6.5 LUT 28 份按机制归组（→ 5 份，带演进叙事）

`LUT_*` 前缀有 28 份、4232 行，但它们不是一个主题。按**机制**归成 5 份，
每份开头新写一节「机制演进」，把该机制被推翻与被修正的过程按时间串起来；原文一字未删，仍按时间排在后面。

| 合并后 | 份数 | 覆盖的机制片段 |
|---|---|---|
| `LUT_SWAP_MECHANISM_2026-10-16_to_11-08` | 11 | **换表闭环**：E4「成片死路」→ A17 打错表 → `SelLUT=bits[5:4]` 钉死 → 4 字节 + 写槽 + 半按（负片目视）→ 退出路径还原 / 改错 `View4[3]` → 秒切取消半按 |
| `LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07` | 6 | **表格式与输入端**：CrCbY 坐标、17³ native、`measure_tone.py` 反解输入曲线 |
| `LUT_LIBRARY_2026-10-25` | 6 | **库**：体检 / 风格复核 / 去重补隙 / 品牌色 / 外部 CLUT 的 512²=64³ 解码坑 |
| `LUT_MIX_AND_LIBRARY_OPS_2026-10-31` | 2 | **混合**：串联不可行 → 逐节点插值 ≡ 输出域混合 → `lutmix.arm` 与三态面板 |
| `LUT_PIPELINE_ROADMAP_2026-10-09_to_11-01` | 3 | **管线**：三路径设计 → 复核 → 「转换与铺设已全在机内」 |

`current/` 144 → **121** 份，`archive/` 99 → **127**。合并后全库相对链接 **0 断链**。

### 6.6 三组大块：重写正文 + 原文归档（19 份 → 5 份）

这三组体积大（1754 / 2631 / 739 行），逐段拼接会得到 2000 行以上的文件。改用**重写**：
正文只留判定、地址、判据与边界，过程推理归原文；原文整份归档，不并入。

| 新文档 | 由哪些组成 | 行数 |
|---|---|---|
| `NX500_AF_STATIC_OVERVIEW_2026-10-31` | 九轮（基线 + ROUND2..9） | 1754 → 130 |
| `AF_ADDRESS_AND_AE_2026-11-17` | 地址图 + AE 耦合 | 529 → 150 |
| `AF_MOTION_AND_LOWLIGHT_2026-11-17` | 低光链 + 运动杠杆 | 1210 → 180 |
| `AF_TRIGGER_AND_PATCH_2026-11-17` | 慢 AF 触发 + 补丁落点 | 892 → 130 |
| `UI_MENU_AND_PW_ABI_2026-10-15` | R1A/R1B/R1C/R1_SUPPLEMENT | 739 → 175 |

三条被这次重组凸显出来的判定：

1. **AF 有两套命名空间**：`0xNNNNN` 属 `CAFData`（330 条），槽位属 `CIQ_SysDataIF_NX1`（184 槽）；
   用错表会得到"自洽但错位"的名字。
2. **主体保持已有、眼部 AF 是缺口**：前者只需调策略，后者要新增两段代码。
3. **低照度不改相位门控**：AE 影响 AF 的唯一正式路径是 LV+ISO → 滤波器（`0x20002/0x20004/0x20007/0x2000C`）。

### 6.7 任务流与进度记录清出 `current/`（7 份）

这类文档的价值在"当时的排期与判断"，不在"现在该怎么做"，且大多已被后续结论覆盖。整份归档，
`current/` 不再保留流水账；需要复盘某天的顺序时按下表按名去 `archive/` 找。

| 已归档 | 内容 | 为什么清出 |
|---|---|---|
| `TASKFLOW_2026-10-16` | 任务流与执行记录 | 排期已过；现行计划看 `P0_EXECUTION_PLAN_2026-11-20` |
| `HANDOVER_2026-10-15` | 旧交接（带 10-18 横幅） | 已被 `HANDOVER_2026-10-16`（总入口）取代 |
| `SESSION_SUMMARY_2026-10-15` | 单日会话总结 | 一次性记录 |
| `NATIVE_UI_LOADING_PROGRESS_2026-10-16` | 原生 UI 加载进度 | 原生 UI（EFL）已被判死，NXUI 取代 |
| `DELIVERY_RESUME_AUDIT_2026-10-16` | 交付恢复审计 | 一次性状态盘点 |
| `RE_PROGRESS_2026-10-08` | 逆向总进度表 | 进度快照；结论已分散到各专题文档 |
| `UI_R0_TARGET_INVENTORY_2026-10-15` | 原版 App 样本与哈希台账 | 台账随样本更新，留在 `evidence/` 更合适 |

保留在 `current/` 的相邻文档：`HANDOVER_2026-10-16`（总入口）、`P0_EXECUTION_PLAN_2026-11-20`（现行计划）、
`ONDEVICE_SOP_2026-10-16`（上机 SOP）、`PATCH_SURFACE_REVIEW_2026-10-12`（补丁空间方法论）、
`U6_ONMACHINE_RESULT_2026-10-09`（E4「成片死路」的原始证据，不能删）。

`current/` 107 → **100** 份，`archive/` 146 → **153**。全库相对链接 **0 断链**。
