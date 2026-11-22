# AF 旋钮与降噪面：可达性清点（2026-11-21）

> 方向由 2026-11-21 决定：AF 可调性 + 降噪/锐化。本文只做**可达性清点**与**AF 工具落地**，
> 不含任何上机结论。相机本轮离线（`.102/.103/.104/.108` 的 22 全 closed）。
>
> 数据来源：`fw/v1.12/p7_wrapped.elf`（objdump 手工反汇编）·
> `test_server/isp/_iqr_raw.txt`（历史 `st cap iqr` dump，184 id）·
> `test_server/slp/_pull_prefslot_info0.txt`（`prefman info 0` 全量 689 键）·
> `raw8/p7/tuning_keys.txt`（11,140 键）。

---

## 0. 一句话

AF 那三个数**地址复核通过、工具已编译待上机**；降噪面的实际情况与初判不同——
**锐度早就通了**（PW 七维之一），真正空着的是**录像 Smart Range** 和 **NANR 的细粒度参数**。

---

## 1. AF 三参数：独立复核通过

### 1.1 指令级复核（本轮，不采信文档转述）

`vaddr → file_off` 按 `- 0x80000000 + 0x1000`：

```
8029b4e4  push {r3,r4,r5,r6,r7,r8,sl,lr}     ← 函数起点，与文档一致
8029b4e8  ldr  r5, [pc,#944]                 → 0x8029b8a0 字面量 = 0x81550220（GAF 基址）
8029b500  str  r3(=4), [r5,#0x230]           → 0x81550450  small = 4
8029b504  str  r3(=4), [r5,#0x234]           → 0x81550454  ok    = 4
8029b558  mov  r2,#8
8029b56c  str  r2,    [r5,#0x22c]            → 0x8155044C  ng    = 8
```

三条写入、基址字面量，**四件互相印证**。`AF_TRIGGER_AND_PATCH_2026-11-17.md` §1 的地址表准确，
无需修正。这条链可以当既有结论用。

### 1.2 同一个构造函数里的相邻字段（第二轮旋钮，留档）

同函数在 `+0x22c..+0x240` 一带连续写了 10 个 int。除上面三个，还有：

| 偏移 | 绝对地址 | 初值 | 备注 |
|---|---|---|---|
| GAF+0x210 | `0x81550430` | 7 | 未知语义，与 AF 频率/阈值同族 |
| GAF+0x214 | `0x81550434` | 35 | 同上 |
| GAF+0x218 | `0x81550438` | 28 | 同上 |
| GAF+0x21c | `0x8155043C` | 17 | 同上 |
| GAF+0x220 | `0x81550440` | 35 | 同上 |
| GAF+0x224 | `0x81550444` | 30 | 同上 |
| GAF+0x238 | `0x81550458` | 18 | **紧邻 ok 之后**，工具不碰 |
| GAF+0x23c | `0x8155045C` | 480 | 疑为超时/步数 |
| GAF+0x240 | `0x81550460` | 400 | 同上 |

⇒ `ng/ok/small` 改完若"有感觉但不够"，这 9 个是下一层。**现在不动**：语义未定，改错方向会
让 AF 行为变差而说不出原因。

### 1.3 工具：`test_server/afknob/nx_afknob.arm`

- md5 `f42c9a9ba6fdd7e30a1a1a8046d2ffd9`，7880 B，`ELF32 ARM EABI5 / statically linked`
- 构建：`test_server/afknob/build_afknob.sh`（WSL `NXKS2`，gcc 13.3.0，`-nostdlib -O0 -marm`）
- 物理可达性假设：与 `nx_regtabwrite` 同一条 `/dev/mem` 通路（后者已实证 `0x80380000` 可读写）

```
nx_afknob.arm status                     # 只读：三值 + 判 STOCK/TUNED/UNKNOWN
nx_afknob.arm set <ng> <ok> <small>      # 写；要求当前 == 8/4/4，否则需 --force
nx_afknob.arm restore                    # 写回 8/4/4
nx_afknob.arm snapshot <path>            # 整页 4 KiB 落盘（对照用）
```

**fail-closed 五条**（任何一条不满足就不写）：

1. 三值必须同时可读且落在 `0..64`，否则判 `UNKNOWN` 并拒绝写入（防基址错时拿垃圾乱写）
2. `set` 要求当前 == stock `8/4/4`，否则拒绝；`--force` 才覆盖
3. 写前整页快照落 `/mnt/mmc/nxwa_afknob/gaf_page.bin`
4. 写后逐字节对照：**`0x44C..0x457` 之外有任何差异立刻报 FAIL**（不碰 `+0x238=18`）
5. 不持久化：p7 重建 AF 管理器或重启即回默认

`slot[0..2]` 的写入顺序是 `ng / small / ok`——**small 在中间**，按参数名顺序写会错位。

### 1.4 待上机验证（三条，按序）

| # | 问题 | 判据 | 失败含义 |
|---|---|---|---|
| V1 | `/dev/mem` 能否读到 `0x81550000` 页 | `status` 打出 `STOCK`（8/4/4） | mmap 被拒 ⇒ 该页归内核，此路不通，转 capdtm 通道 |
| V2 | 写入是否被 p7 接受 | `set` 后 `READ_*` 三值一致 + 退出时 `status` 仍为新值 | p7 每帧重写或另有初值源 ⇒ 需找写入点而非改内存 |
| V3 | 效果是否可感 | AF 在低对比/运动场景的丢焦次数（录像判据） | 值变了但行为不变 ⇒ 消费点不在跟踪逻辑，或该参数被上层覆盖 |

**V1 只需 `status`**，零写入风险。V1 不过就不要做 V2。

### 1.5 三组预设（上机可直接试）

| 名字 | ng | ok | small | 预期手感 |
|---|---|---|---|---|
| snappy | 4 | 2 | 2 | 锁定快、丢得快，适合静态主体/单点 |
| stock | 8 | 4 | 4 | 原厂 |
| sticky | 20 | 6 | 8 | 慢但死守，适合连续追拍/运动会掉的目标 |

方向依据：`ng` 是"连续 NG 几次停止跟踪"，调小＝更早放弃；`ok` 是"连续 OK 几次开始跟踪"，
调小＝更快锁定；`small` 是"小目标抖动次数"，调大＝对小目标更宽容。

### 1.6 本线最大的技术风险：p7 的 cache

Linux 侧写进 DDR，**p7 未必看得见**。AF 每帧读这三个 int，如果 p7 把它们缓存在 L1/L2，
改 DDR 就不生效——`status` 会显示新值（Linux 读自己的写），但 AF 行为纹丝不动。

这决定了 V3 失败时不是"参数没消费"，而是**两条完全不同的路**：

| V3 现象 | 结论 | 下一步 |
|---|---|---|
| 行为有变化 | 通路成立 | 做成用户选项（NXUI/FilmLab 侧） |
| 行为不变 | 可能是 cache 失效 | 找 p7 的 cache 操作（`d5_ipc_*` 里有没有 flush/`mc` 指令），或改为**写后触发一次重读** |
| 行为不变且无法解 cache | 退化为 p7 补丁 | 改 `0x8029B558` 的 `mov r2,#8` 立即数（A2 patch pack 已有流程），代价是需重新签名装载 |

所以 V3 的判据必须**同时**记 iqr/行为与"改后 `status` 是否仍为新值"，否则无法区分"没生效"
与"生效但看不见"。


---

## 2. 降噪 / 锐化：可达性地图

### 2.1 先纠正初判

我在 11-21 的方向评估里说"锐化空白"，**这条是错的**。PW 七维是
`R/G/B/HUE/SAT/**SHARP**/CON`，锐度早就在 FilmLab 通道里可写（`PW_BASE=0xa3ec`，`SHARP` 为第 5 维）。
`iqr` 里 `[86] eIQ_ID_PW_SHARPNESS = 0x000FD80A`，与 HUE/SAT/CON 同值，因为四维打包成一个 32 位
scalar 写。

### 2.2 三层入口的实际状态

| 层 | 入口 | 现状 | 判据 | 结论 |
|---|---|---|---|---|
| 用户态 prefman | `APPPREF_EFFECT_*_SHARPNESS`（14 个风格各一份，`0xa3e0`/`0xa4f0..0xa520`） | 已可写（FilmLab 在用） | `eIQ_ID_PW_SHARPNESS` 读回 | **已通，无新工作** |
| 用户态 prefman | `APPPREF_MOV_SMART_RANGE` @`0xa380` | 存在但从未被使用 | `eIQ_ID_SMARTRANGE_MOVIE` | **空白** |
| iQ 参数面 | `eIQ_ID_HIGH_ISO_NR` = `eHIGH_ISO_NR_MID`(2) | 原生菜单已有（OFF/LOW/MID/HIGH） | iqr 读回 | **已有，非新能力** |
| iQ 参数面 | `eIQ_ID_LONG_TIME_NR` = `ON`(1) | 原生菜单已有（OFF/ON） | iqr 读回 | **已有** |
| iQ 参数面 | `eIQ_ID_SMARTRANGE_CONFIG` = `HDR_MIDDLE`(3) | 照片侧在跑（OFF/ON/HDR_LOW/MID/HIGH 五档） | iqr 读回 | 已有 |
| iQ 参数面 | `eIQ_ID_SMARTRANGE_MOVIE` = `OFF`(0) | 视频侧关着 | iqr 读回 | **空白（可开）** |
| p7 硬件层 | `d5_ipc_set_subparam_nanr` + `_2nd_*` 同族 | 无用户态入口 | 无（iqr 里没有 NANR 项） | **真空白** |

### 2.3 两块空白的性质完全不同

**（A）录像 Smart Range —— 便宜、可判、真新增**

`eIQ_ID_SMARTRANGE_MOVIE` 在 iqr 里可读可判（0→1 立即可见），`APPPREF_MOV_SMART_RANGE` 在
prefman 里有槽（`0xa380`，4 字节）。原生菜单只给了照片的"智能范围"，视频侧一直是关的。
一条 `prefman set 0 0xa380 l 1` + iqr 读回就能裁决：是"菜单没暴露"还是"写了也不进 ISP"。

**（B）NANR 细粒度 —— 贵、判据弱、要新增调用点**

`tuning_keys.txt` 里 NANR 是一整族，说明它有两级降噪、每级 5 个参数：

```
d5_ipc_set_subparam_nanr
d5_ipc_{get,set}_subparam_nanr_{sigma, std_g, std_limit, std_ref, blend}
d5_ipc_get_subparam_nanr_2nd_{sigma, std_g, std_limit, std_ref, blend}
d5_ipc_set_subtop_ctrl_nanr1 / nanr_ctrl0
nanr_mem_mode
```

结构上和曲线（`d5_ipc_set_subparam_tc`，108 字 DDR 拷贝）**同族**——同一条 IPC 通道、同一种
"拷贝一堆参数进 DDR"的形态。所以 `CURVE_ANATOMY` 的方法可以直接搬：找函数体、找目标 DDR 区、
找参数集提交点。

但**没有 iqr 判据**（`iqr` 184 个 id 里没有 NANR），只能靠画面（噪点标准差 / 暗部色块）。
这决定了它的验收成本远高于 AF 那条。

---

## 3. 建议顺序

1. **相机上线后先跑 `nx_afknob.arm status`**（零风险，一条命令回答 V1）
2. V1 过 → 做 V2/V3，重点是"值是否被 p7 保留"和"AF 行为是否可感"
3. 并行做录像 Smart Range 的裁决（`prefman get 0 0xa380` → `set` → `iqr` 读回）
4. NANR 留到有整块时间时，按 `CURVE_ANATOMY` 的方法做静态定位；先做**只读**（找 DDR 区、读现状），
   再决定是否新增调用点

---

## 4. 未闭合

| # | 项 | 为什么没闭合 |
|---|---|---|
| 1 | `0x81550000` 页是否可 mmap | 需上机 |
| 2 | `ng/ok/small` 的消费点是否只在跟踪逻辑 | 静态只证写入，未证读 |
| 3 | `+0x210..+0x240` 那 9 个 int 的语义 | 未逆向 |
| 4 | `APPPREF_MOV_SMART_RANGE` 与 `eIQ_ID_SMARTRANGE_MOVIE` 是否同一条链 | 需上机对照 |
| 5 | NANR 参数集的 DDR 目标地址 | 未定位 |
| 6 | `_iqr_raw.txt` 的快照日期与固件版本 | 未标注，当历史参考 |

---

## 5. 现场纪律（本轮新增）

- `nx_afknob.arm` 的 `set` **只在 `status` 返回 `STOCK` 后才允许**；`--force` 不用于首次试验
- 改完若要复原：`restore`，或直接重启（构造期重写）
- 与 `rdeeprom` 那类 `reg_mem` ioctl 家族无关——这里只走 `/dev/mem` 的 mmap，不碰 SDIO
