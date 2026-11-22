# AF 地址体系与 AE 耦合（2026-11-17）

> **2026-11-20 重写**：原 `AF_ADDRESS_MAP_2026-11-17.md`（88 行）与 `AF_AE_COUPLING_2026-11-17.md`（441 行）
> 已归档到 `docs/archive/`。本文只保留可引用的地址裁定、接口与判据。
> 性质：纯静态（capstone `CS_MODE_ARM`），未连相机。镜像 `fw/v1.12/p7_wrapped.elf`。

---

## 0. 地址约定（先统一，否则后面全错）

```
runtime  = chunk + 0x80000000
file_off = chunk + 0x1000
chunk    = runtime − 0x80000000
vaddr    = file_off − 0x1000 + 0x80000000
```

镜像只有 **一个 PT_LOAD**：`vaddr 0x80000000`、file off `0x1000`、filesz `0xB84280`
（覆盖 `0x80000000–0x80B84280`）。其余地址（`0x81xxxxxx` 的 DDR 全局）**不在文件里**，
只能给地址、不能给初值。

> 这条约定是 11-17 那轮"修正上一轮"的核心：此前多份文档把 file offset 当 vaddr 用，
> 差一个 `0x1000`，表现为"同名函数在 `0x34470c` 找不到，实际在 `0x34570c`"。

---

## 1. 字符串锚点法：不采信任何一方的地址，只看指令

方法：扫 chunk `0x20000–0x570000` 的全部 32 位字，识别 `movw` + `movt` 同寄存器对，
组合出目标地址；落在字符串区（`0x80600000–0x80780000`）且可解为 ASCII 的即为候选引用。
函数起点用 `push {…,lr}`（`(w & 0x0FFF0000) == 0x092D0000 && bit14`）建 **17,612** 个候选，
取"不大于引用点的最大起点"。

结果：**821** 条字符串引用，其中 **73** 条与 AF 相关。原始表 `E:/NXWA/tmp_afaddr/af_refs.txt`。

裁定：**文档地址体系基本正确，个别标注错**。抽样对照：

| 锚点 | 字符串 | 引用点 | 函数起点 | 与旧文档 |
|---|---|---|---|---|
| `SetHoldFaceBox` | `SetHoldFaceBox %d` | `0x80140DF8` | **`0x80140DE8`** | ✅ 与 `0x140de8` 一致 |

### 1.1 被这轮修正的地址（`+0x1000` 类错误）

| 旧值 | 正确值 | 依据 |
|---|---|---|
| `0x34470c` `proPAOToContrastAFCheck` | **`0x34570c`** | 同名锚点定位 |
| `0x3406dc` `proPAOAARGetUseContrastAF` | **`0x3416dc`** | 同上 |
| `0x34a13c` `CAFSequenceNxRoughFineScan::Initialize` | **`0x34b13c`** | 同上 |
| `0x2f0ce4`「相位许可六输入」 | **无法复现**（`0x2f1ce4` / `0x2efce4` 都对不上） | 见 §2.3 |

---

## 2. AE → AF 的唯一正式接口

### 2.1 `AeCalcAFParamContiConti` 不是"大计算函数"

`0x802D8ED8` 只有 `0x70` 字节，是个**协调器**：把 3 个子函数串起来，结果全部写进
**AE 上下文单例 `0x8156EAF8`**；它**不直接写 AF 模块**，**也不直接调 IQ `SetParameter`**。

输入只有亮度统计：liveview 人脸平均 Y + 221 bin 加权直方图 + 8 区亮度和。
输出：**1/16 EV 单位的亮度偏移**，以及 EV / LV 当前值。

### 2.2 唯一确定的正式接口：LV 与 ISO

```
AE 上下文 0x8156EAF8
    ↓  AF 侧用 0x802C8908(ae, idx) 读（eCALC_LV_CURR 与 ISO）
UpdatetHpfLpfFrequencyCoring  0x803351A0
    ↓  写 AF 参数 0x20002 / 0x20004 / 0x20007 / 0x2000C
SetAfFilterToDsp  0x803353D4
    ↓
DSP 的 AF 滤波器
```

直接证据（字符串）：
`0x8074AB2C`「UPDATE HPF-CORING(...)(ISO:%d)」、`0x8074ABA0`「UpdateAFRegister::ISO[%d]」。

> ⇒ **AE 影响 AF 的方式是"调滤波器"，不是"改相位许可"**。这一点与
> `AF_MOTION_AND_LOWLIGHT` 里"低照度不禁用相位"的结论互相印证。

### 2.3 被推翻的一个身份判断

`0x8034BEBC` 的真身是 **`saveAfState` —— AF 状态轨迹记录器**（环形缓冲 + 互斥 +
`[GROUP] AF STATE` 日志），**不是** AF 执行阶段的状态切换器。`mode=1/2` 只改被记录的值，
不影响控制流。

---

## 3. 判据与边界

| 项 | 内容 |
|---|---|
| 判据 1 | 任何地址先进"file ↔ vaddr"换算再比对，差 `0x1000` 的错误在旧文档里出现多次 |
| 判据 2 | 函数边界必须回溯到真实 `push {…,lr}`，不能线性扫过函数边界 |
| 判据 3 | 字段归属用寄存器级轻量污点，避免"不同对象同偏移"误判 |
| 边界 1 | `0x81xxxxxx` 的全局（AE 上下文、GAF）不在镜像里 ⇒ **只有地址，没有初值** |
| 边界 2 | 本文不含运行时验证；"接口存在"≠"该接口在当前固件配置下被调用" |

---

## 4. 原文

`docs/archive/AF_ADDRESS_MAP_2026-11-17.md`（821 条引用的原始表、逐条裁定）、
`docs/archive/AF_AE_COUPLING_2026-11-17.md`（三子函数拆解、调用图、污点分析过程）。
