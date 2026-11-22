# -*- coding: utf-8 -*-
"""LUT 28 份按机制归组，每组带演进叙事。"""
import os, re, json

CUR = 'docs/current'

SWAP_INTRO = """## 机制演进（读这一节就够）

这条线一共错了四次，每次都把"不可能"改成"条件没满足"。按时间：

### 0. 起点：一个被外推过度的负结论（10-09）

U6 上机把 Portra 装进 LUT1，半按、松开、全按，成片没有风格；Still 期 `Cfg` 从 `0x0` 变成 `0x1100`。
据此写出 E4：**「成片直灌 LUT 是死路」**。后来的记录承认，这是把"某一条序列的负结果"外推成了机制结论。

### 1. 重启与假设：打算让 sel=0 保住 LUT0（10-16）

LUT 线解冻。方案是"改灌 LUT0 + 用守护阻止 p7 重灌"。复核时发现守护只恢复**选择配置**、
不重灌**内容**，而 p7 可以在 sel 恒为 0 时自己触发 Load —— 这条路有代码层缺口。

### 2. 第一个转折：打错了表（10-17）

实机只读取值：当前 Load 源是 **A17 `0x81115200`**（肤色表），而 10-09 的负片实验写的是 **A12
`0x810FD100`**（标准池）。于是"写了表画面不变"**不是**地址错、更不是换表不可行，而是
**从来没写到 p7 真正在用的那张表**。E4 的解读从这里开始翻转。

同一轮还补上了此前真正缺的一环：p7 启动时把整段已初始化数据从镜像地址搬到运行地址，
42 个候选槽都在这段复制范围内 —— 逐槽搜常量引用找不到填充者，不等于没有填充者。

### 3. 位定义钉死，登记表可信（10-18 上午）

两件基础事实在同一轮闭合：

- **`SelLUT = Cfg bits[5:4]`**，bit8 是 LUT0 格式位、bit12 是 LUT1 格式位。
  ⇒ E4 里那个 `0x1100` 其实是 **SelLUT=0**，与 T4 记录的"Still 用 sel=0"不冲突；
  此前"bit12=SelLUT 所以矛盾"的判断**撤回**。
- **三张静态登记表在运行时地址上可读，且与记录逐项一致**（View4 4/4、Still6 6/6、BV12 12/12）；
  所在 16 KiB 代码区与镜像 `0x380000` **逐字节相同**（md5 `eec72668…`，diff=0）。

⇒ 登记表不是黑盒，改它等于改 p7 的装载目标。

### 4. 决定性一步：4 字节 + 写槽 + 半按（10-18 下午）

不刷机、不改固件：

1. 改 **1 个 4 字节登记表项**（`View4[3]`），`load_addr` 由 `0x81115200` → `0x81119f00`；
2. 往空闲槽写 **19,712 字节**（4913 项全部 `255−R` 反相）；
3. 半按触发重载 ⇒ **取景器出现明显负片**（用户目视确认）。

四条同时成立：登记表所在页可写；p7 **每次 Load 都重读登记表项**（`cfg`/`sel` 全程不变，
只有 `load_addr` 变）；改一个 word 就能改硬件装载源；写进槽的内容**真被渲染**。
同日更早的 sel=0 后端路线当场作废 —— 改动更大，收益相同。

### 5. "装了不生效"的两次误判（10-19）

- **退出路径还原**：`lut.sh apply` 之后 p7 还没来得及装载，`lut.sh off` 已经跑完 ——
  三条退出路径里只有一条会保住结果。数据通路没坏，是自己把表还原了。
- **改错了登记项**：当时只改 `View4[3]`，而相机"屏幕色彩"开启、输出 sRGB，实际选的是
  **`View4[1]`**。修法 = 双登记项 + 正常事件重载，连续换表与 off 恢复实机通过。

切换通过 ≠ 色彩正确：紧接着发现导入表严重偏色，那是另一条线（见 `LUT_FORMAT_AND_INPUT`）。

### 6. 秒切：把"必须半按"也推翻（11-08）

`lut.sh apply` / `off` 现在自己完成 Load，面板点一张取景器立刻变。
旧结论「不能从 ARM 侧强制 reload，只能半按」**只对"裸写 `+0x00c` + 脉冲"成立** ——
当年漏了两个必要条件。

同日还有两笔：`nx_lutptr.arm set` 把 p7 的 **23 项档位指针表**（View4 + Still6 + BV12 整族）
整体指到 A18，录像**首帧**即用户表（连续录 4 分钟无异常）；闪屏用 **CORE RSZ_OUT1_EOF（irq 14）**
等帧结束再装载解决，**"一帧是物理下限"的结论撤回**。

### 当前机制（一句话）

p7 每次 Load 都重读登记表项 ⇒ 改登记表就是改装载目标；槽内容会被真实渲染 ⇒ 写 19,712 字节即可换表；
两者都可双向回滚，断电即恢复原状。
"""

FORMAT_INTRO = """## 机制演进

### 0. 切换修好之后才暴露的问题（10-19）

登记表修好、画面能变之后，用户报告导入的 `hp5`、`fuji400h` 等几乎全部严重偏色。
中性灰阶 + 色度方向 + 两个真实 LUT 对照确认：**链路里表的坐标顺序是 Cr、Cb、Y**，
而旧转换器直接按 RGB 存 ⇒ 红蓝互换、中性轴错位。

修法是重出候选（BT.601 全范围），用户目视确认 HP5 黑白正常、Fuji400H 颜色恢复。
边界：红蓝顺序与中性轴有实机证据，**精确色彩标定没做**，不能称为"经过测量的相机色彩配置"。

### 1. 机内修正与回归复核（10-25）

CrCbY 修到机内转换器；同时做色彩回归复核，确认不是所有表都错、也不是所有表都对。

### 2. 网格：为什么是 17³（10-31）

硬件/固件吃的是 **17³ native**；33³ 必须下采样，收益不抵损失。这条决定了 `cube2lut` 的烘焙目标。

### 3. 输入端怎么测（11-07）

3DLUT 在 JPEG 编码器**之前** ⇒ 拍一张 JPG 就能反推输入端特性；
`measure_tone.py` 从灰阶反解 gamma / 肩部 / 趾部。至此"输入是什么样的"不再靠猜。
"""

LIB_INTRO = """## 库是怎么长起来的

### 0. 体检（10-25）

319 行的库体检：先量清库里到底有什么、哪些重复、哪些不合规。

### 1. 风格复核与补隙（10-25）

风格复核 V2 + 去重补隙：同一风格多份、不同风格撞色的问题在这一步收敛。

### 2. 品牌色（10-25）

按品牌/胶片型号归档，建立可检索的命名。

### 3. 外部来源的解码坑（10-25）

外部 CLUT 导入踩过的坑：**512² 的 Hald PNG 实际是 64³**，索引 `idx = y*512 + x` 且**蓝通道变化最快**；
G'MIC 的 `.cube` 自校验测不出，只表现为 `hue_err ≈ 145°`。判据靠常识：
Portra400 的 `(208,48,48)` 应变成 `(251,65,23)`。

### 4. 物理胶片配方（10-25）

最短的一份，记录胶片实拍配方的取色来源。
"""

MIX_INTRO = """## 演进

### 0. 先证明"两张表不能串联"（10-31）

`DUAL_LUT_FEASIBILITY` 证明串联不可行 ⇒ 混合是它的替代答案。

### 1. 混合的数学（10-31）

逐节点插值 ≡ 输出域混合。这条等价关系决定了强度可以做成"一个 0–100 的数"，
而不是两张表各一套参数。可辨粒度与三个必须先定的语义也在这里确定。

### 2. 做成机内动词（11-01）

`lutmix.arm` 落地：`list / fav / move / del / index / stock / mix / strength`；
面板加 BROWSE / EDIT / MIX 三态，`order` 文件里 `*` 表示收藏，删除走 `looks/_trash/`。
同轮补了两套库的机内增删与排序。
"""

PIPE_INTRO = """## 演进

### 0. 管线设计（10-09）

三条转换路径与格式契约的初版，回答"cube 怎么变成机内表"。

### 1. 复核（10-18）

对路线与资产做一次独立复核，砍掉不可行的分支。

### 2. 现状收口（11-01）

结论：**转换与铺设已经全在机内**（10-18 起），漏的只有"文件怎么进卡"这一步 ——
而 FTP/SD 都能做。至此 LUT 线不再依赖电脑。
"""

GROUPS = [
 ("LUT_SWAP_MECHANISM_2026-10-16_to_11-08.md", "LUT 换表机制：从「成片死路」到「4 字节 + 半按」", SWAP_INTRO,
  ["LUT_ROADMAP_RESTART_2026-10-16.md", "LUT_RUNTIME_SOURCE_AUDIT_2026-10-17.md", "LUT_A17_ONDEVICE_C1_2026-10-17.md",
   "LUT_REGTAB_READ_2026-10-18.md", "LUT_RETARGET_LIVE_2026-10-18.md", "LUT_SEL0_ROUTE_REVIEW_2026-10-18.md",
   "LUT_UI_2026-10-18.md", "LUT_SWITCH_NOT_EFFECTIVE_2026-10-19.md", "LUT_STABLE_BASELINE_HISTORY_2026-10-19.md",
   "LUT_SWITCH_REFRESH_FIX_2026-10-19.md", "LUT_INSTANT_SWITCH_2026-11-08.md"]),
 ("LUT_FORMAT_AND_INPUT_2026-10-19_to_11-07.md", "LUT 表格式与输入色彩：CrCbY、17³、输入端校准", FORMAT_INTRO,
  ["LUT_COLORSPACE_AUDIT_2026-10-19.md", "LUT_CRCBY_FIX_2026-10-19.md", "LUT_CRCBY_ONDEVICE_FIX_2026-10-25.md",
   "LUT_COLORSPACE_REGRESSION_REVIEW_2026-10-25.md", "LUT_GRID_17_VS_33_2026-10-31.md", "LUT_INPUT_CALIBRATION_2026-11-07.md"]),
 ("LUT_LIBRARY_2026-10-25.md", "LUT 库：体检、去重、品牌色与外部导入", LIB_INTRO,
  ["LUT_AUDIT_2026-10-25.md", "LUT_STYLE_AUDIT_V2_2026-10-25.md", "LUT_DEDUP_AND_GAPFILL_2026-10-25.md",
   "LUT_BRAND_LOOKS_2026-10-25.md", "LUT_WEB_SOURCE_2026-10-25.md", "LUT_PHYSICAL_FILM_2026-10-25.md"]),
 ("LUT_MIX_AND_LIBRARY_OPS_2026-10-31.md", "LUT 混合与机内库操作", MIX_INTRO,
  ["LUT_STRENGTH_MIX_2026-10-31.md", "LUT_LIBRARY_AND_MIX_2026-11-01.md"]),
 ("LUT_PIPELINE_ROADMAP_2026-10-09_to_11-01.md", "LUT 管线与路线：设计、复核、收口", PIPE_INTRO,
  ["LUT_PIPELINE_DESIGN_2026-10-09.md", "LUT_WORKBUDDY_REVIEW_2026-10-18.md", "LUT_ONDEVICE_PIPELINE_2026-11-01.md"]),
]


def split_h1(text):
    lines = text.splitlines()
    title, start = None, 0
    for i, ln in enumerate(lines):
        if ln.startswith('# '):
            title, start = ln[2:].strip(), i + 1
            break
        if ln.strip() and not ln.startswith('>'):
            break
    return title, '\n'.join(lines[start:]).strip('\n')


def demote(body):
    out, fence = [], False
    for ln in body.splitlines():
        if ln.strip().startswith('```'):
            fence = not fence
            out.append(ln); continue
        if not fence and re.match(r'^#{1,5} ', ln):
            ln = '#' + ln
        out.append(ln)
    return '\n'.join(out)


mapping, report = {}, []
for newname, title, intro, members in GROUPS:
    secs, toc = [], []
    for idx, m in enumerate(members, 1):
        raw = open(os.path.join(CUR, m), encoding='utf-8', errors='ignore').read()
        h1, body = split_h1(raw)
        h1 = h1 or m
        body = demote(body)
        for j, other in enumerate(members, 1):
            if other != m:
                body = body.replace('`%s`' % other, '本文 §%d' % j)
        toc.append('%d. %s' % (idx, h1))
        secs.append('## %d. %s\n\n> 原文：`%s`（已归档至 `docs/archive/`）\n\n%s' % (idx, h1, m, body))
        mapping[m] = newname
    head = ('# %s\n\n> **2026-11-20 文档合并**：本文由 %d 份记录按时间顺序合并，原文未删改、标题层级下调一级；'
            '原始文件归档在 `docs/archive/`，见各节「原文」。\n\n%s\n\n## 原文清单\n\n%s\n\n---\n\n'
            % (title, len(members), intro, '\n'.join(toc)))
    open(os.path.join(CUR, newname), 'w', encoding='utf-8').write(head + '\n\n'.join(secs) + '\n')
    report.append((newname, len(members), sum(1 for _ in open(os.path.join(CUR, newname), encoding='utf-8'))))

for n, c, l in report:
    print('%-56s %2d 份 %5d 行' % (n, c, l))
json.dump(mapping, open('/tmp/merge_map2.json', 'w'), ensure_ascii=False, indent=1)
print('合并 %d 份 → %d 份' % (len(mapping), len(GROUPS)))
