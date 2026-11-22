# 重装固件：能力清点与真实缺口（2026-11-21）

> 起因：一系列侵入性实验（p7 DDR 写、EP/DMA、mlanutl）之后，需要一个"能刷回去"的底。
> 本文回答三件事：**已经有什么 / 能覆盖到哪一层 / 真正缺什么**。
> 全部为本地离线工作，相机未参与。

---

## 0. 一句话

**固件包的自制能力早就有了，而且今天重新被证明**（工具重建 + 三项自证）；
真正缺的不是"bin"，是 **SLP 覆盖不到的那几个分区没有任何备份**。

---

## 1. 三项自证（本轮新做）

原 `raw8/fw/slp_pack.py` 已不在仓库，工具按 `SLP_CONTAINER_FORMAT_2026-10-08.md`
重写为 **`raw8/fw/slp_tool.py`**（list / verify / extract / repack / patch / bump）。

| # | 测试 | 结果 |
|---|---|---|
| 1 | `verify` 官方 `NX500_v1.12.bin`（352,121,435 B） | **10/10 段 JAMCRC 全中**，平铺无断裂，EOF 对齐，`VERDICT PASS`（1.9 s） |
| 2 | `extract` → `repack` → `cmp` 原包 | **逐字节相同**，md5 双侧均为 `df08b6f802d548ed03bb9e974c9a9bbd` |
| 3 | 复现 2026-10-11 实刷成功的那只包（img8[0]=01 + version 1.13） | md5 = **`910b9c28336d88bc39857576c6cb3a07`**，与 `gates/u4/acceptance.json` 记录**完全一致** |

第 3 条最硬：那只包当时被官方 UI 接受、写入 eMMC、回读逐字节一致。
⇒ **今天重写的工具，能复现一个被实机刷写器接受过的产物。** 工具链等效性成立。

`slp_a2_restore_v119.bin` 同样 `VERDICT PASS`。

---

## 2. 官方 1.12 包的十段（实测）

| img | len | offset | crc (JAMCRC) | 内容 | 落到分区 |
|---|---|---|---|---|---|
| 0 | 6,160,624 | `0x0000_0130` | `0x48f3183b` | uImage（Linux 3.5） | vImage / 内核 |
| 1 | 56,289 | `0x005e_0220` | `0x19d433b2` | ARM 向量表，tail=`BABF43A` | **boot0 @512** |
| 2 | 3,176,040 | `0x005e_de01` | `0xd8719107` | uImage | p6 |
| 3 | 6,169,816 | `0x008f_5469` | `0x2180947f` | uImage | p13 |
| 4 | 117,184 | `0x00ed_7941` | `0x8785210e` | 偏移/长度表 | **boot1** |
| **5** | **12,075,648** | `0x00ef_4301` | `0xdf679d3b` | **p7 / rtos** | p7 |
| 6 | 280,505,658 | `0x01a7_8581` | `0xbda66ec1` | LZOP = rootfs | **p10** |
| 7 | 5,022,551 | `0x125f_b2bb` | `0x41191fb5` | LZOP | p11 (`/opt`) |
| 8 | 28,672 | `0x12ac_5612` | `0xef2256df` | 全零（pcache） | p12 |
| 9 | 38,808,649 | `0x12ac_c612` | `0xb5082de3` | LZOP（snapshot） | p9 |

★ **p14（`/opt/usr`，2.3 G，Metol 装在这里）不在 SLP 里**（`image:none`）。
⇒ **刷任何固件都不会动 mod。** 这既是好事（刷机不清 mod），也说明"想用固件包安装 mod"这条路在 SLP 层是堵死的。

★ img6/img7/img9 是 **LZOP 压缩**，不是分区原始镜像。所以"把当前机器 dump 成固件包"不可行——
重新生成 LZOP 并与刷写器的解压预期对齐，成本远高于收益。

---

## 3. 三层恢复能力，现状对照

| 层 | 手段 | 覆盖 | 状态 |
|---|---|---|---|
| L1 | 相机菜单刷 `/mnt/mmc/nx500.bin` | 十段全量 | ✅ **已实刷验证**（U4 接受 + U5 双向回滚闭环） |
| L2 | `st firmware up`（读 `/sdcard/<project>.bin`） | 十段全量 | ◐ 跳过 CRC，**未实刷** |
| L3 | Main PCB Recovery（拆机 + 短接 TP + `dnloader.bin`） | bootloader 层 | ❌ **无料** |

L1 的两个实测细节（来自 `gates/u4/acceptance.json`）：

- **同版本包被判灰**（UI 灰不可点）；`version_user` +1 后菜单才变亮 ⇒ 自制包必须 bump 版本
- 刷完 rootfs 不动，`/etc/version` 仍显示 1.12（SLP 版本串与设备版本显示是两回事）

---

## 4. ★ 真正的缺口：分区备份丢了

### 4.0 已补回（2026-11-21 12:51 实测）

相机上线后立即用 `test_server/fw/partition-backup.sh run` 复采 13 个分区（443 MiB），
取回 `_deliverables/fwbackup/fwbackup/`，与 10-11 基线逐项对照：

| 结果 | 分区 |
|---|---|
| **✅ 逐字节一致（9 个）** | boot0 / boot1 / p1 / p3 / p5 / p6 / p8 / p13 / p12（纯净态 `4b34d5e8`，比基线更干净） |
| ⚠ 不同，已解释 | **p7**（A2 补丁态，见 4.0.1）、**p2**（用户设置缓存，见 4.0.2） |

⇒ **引导链（p1/p3/p5/boot0/boot1）自 2026-10-11 起一个字节都没被动过。**
与"实验只碰 p7 的 DDR 和 SD 卡"这条认知一致。

#### 4.0.1 p7 的 90 字节 = A2 补丁，且这是第一次确认它已刷

- 机上 p7 前 12,075,648 B 与 SLP img5 只差 **90 字节**，首个差异 `@0x11504`(=70916)
- 该偏移正是 `fw/v1.12/patches/a2_bytepatch.json` 的 `block1.off`；`block2.off`=70960 也落在差异区间
- 整段前缀 md5 = **`712c6fd0a49371ec05ce94c367b0d5c7`**，与 json 记录的 `p7_a2.bin` md5 **完全一致**

★ 这条修正旧认知（"A1/A2 p7 补丁未刷"）。**补丁已在机上生效**——它服务的是
**FilmLab 的 PW 七维直写通道**（外部 id `0x134..0x13A` → 槽 `0xdc..0xe2`，`0x13B` 提交），
见 `A2_PATCH_AND_READBACK_2026-10-14.md` §1–§3。

★ **A2 与 LUT 换表无关**。LUT 走的是 `nx_regtabwrite` 改登记表（View4/Still6/BV12）+ 写 A18 槽 +
`nx_lutload`，全程用户态直写 DDR/寄存器，不经过 p7 的 id 归一化路径。
⇒ 把 p7 回滚成原版，失效的是 **FilmLab**，不是 LUT。这条在 2026-11-21 被用户当场纠正过一次，
原稿写成"LUT 会失效"是错的。

p7 整分区 30 MiB 的尾部 19,381,120 B **全零**，恢复时只需写前 12,075,648 B。

#### 4.0.2 p2 是用户可变区，不构成风险

10 MiB，连续两次 `dd` 结果相同（静态，非活跃写入），但内容与 10-11 基线不同。
非零字节仅 1,788（0.017%），可打印串为
`High-speed` / `Gorgeous night` / `Memories` / `Cinematic` / `/opt/apps/pre_0.JPG…pre_3.JPG`
⇒ **场景模式名 + 预览图路径 + 一组单字节设置值**，是用户设置/缓存，不是校准或序列号。
⇒ 变化无害，**不应作为"分区被破坏"的证据**。

### 4.1 原始缺口说明（历史）

`raw8/emmcbak/MANIFEST.txt` 记录 2026-10-11 用 "整块 dd → SD → FTP 拉回 + 逐 md5 校验" 备份了：

```
boot0 / boot1 / p1 / p2 / p3 / p5 / p6 / p7 / p8 / p12 / p13   （ALL-OK）
```

**现在目录里只剩 `p12_pristine.bin`（5 MB）。其余 10 个文件全部不在了。**

为什么这比"没有固件包"严重：

| 分区 | SLP 里有吗 | 丢了意味着 |
|---|---|---|
| boot0 / boot1 | ✅ img1 / img4 | 可从固件包恢复 |
| p6 / p7 / p9 / p10 / p11 / p12 / p13 | ✅ img2/5/9/6/7/8/3 | 可从固件包恢复 |
| **p1 / p2 / p3 / p5 / p8** | ❌ **不在 SLP** | 见下方 `parttab` 真实身份 |

### 4.2 ★ 官方分区表（`/etc/parttab`，2026-11-21 实读）

这份表推翻了我此前"p1/p2/p3/p5/p8 都不可再生"的笼统判断：

| vol | 设备 | 大小 | image | 真实身份 | 不可再生？ |
|---|---|---|---|---|---|
| **adj** | p1 | 20M | none | **校准数据**（`nvd_cap`/`nvd_iq`/`nvd_vfpn`/`nvd_cs`/`nvd_dpc` 全读写它） | **★ 是，最珍贵** |
| rtos | p7 | 30M | rom.bin | p7 固件 | 否（SLP img5） |
| **rtos_data** | **p8** | 50M | **none** | **p7 运行时数据区** | **否——实测 50 MiB 全零** |
| pref | p2 | 10M | none | 偏好设置 | 否（可变） |
| pref_default | p3 | 30M | pref_default.bin | 出厂默认设置 | 否（固件包带） |
| pref_recovery | p5 | 20M | none | 恢复用设置 | 否（p1 同 md5 的副本之一） |
| opt-usr | p14 | — | none | `/opt/usr`（Metol 本体） | 可重建 |
| rImage | p13 | 10M | rImage | recovery 内核 | 否（SLP img3） |
| pcache | p12 | 5M | pcache.list | 照片缓存 | 否 |
| devicem4 | boot1 | 4M | devicem4.bin | M4 协处理器固件 | 否（SLP img4） |
| snapshot | p9 | 100M | snapshot.img | 快照 | 否（SLP img9） |
| uImage | p6 | 10M | uImage | 内核 | 否（SLP img0/2） |
| bootloader | boot0 | 4M | bootloader.bin | bootloader | 否（SLP img1） |

**结论修正**：

- **真正不可再生的只有 p1（adj，校准数据）**——IQ / VFPN（固定模式噪声）/ 色阴影 / 坏点校正，
  每台机器独有，烧了就得返厂重做调整。
- **p8 实测 50 MiB 全零**，`image:none`，是 p7 的运行时数据区，**丢了零损失**。
- p2 是偏好设置（内容为 `Cinematic`/`Memories`/`pre_N.JPG` 等场景名），可变。
- p3 / p5 是默认设置与恢复设置，p3 由固件包提供。
- `p1 == p5` 的 md5 相同（`284c2ea5…`）这件事，与 `parttab` 的 vol 语义不一致，
  可能是 eMMC 上两个分区恰好同内容（都存了一份 adj 副本），**需要时以 `parttab` 的挂载语义为准**。

★ 上一版本文把 p8 误列为"SLP 覆盖"。按十段长度逐一核对（SLP 无 50 MB 段），**p8 不在包内**，已更正。

> **重要区分**：丢的是**备份副本**，原物仍在这台相机的 eMMC 里。
> 只要相机还能启动到 shell，`dd` 出来即可（见 §5.1）；`MANIFEST` 里的旧 md5 就是验证标准。
> 真正的"无来源"只在**机器已经不能启动**时才成立。

---

## 5. 建议顺序

| 优先级 | 动作 | 代价 | 理由 |
|---|---|---|---|
| **P0** | 相机上线后**立刻重做全分区 dump**，至少三副本（本机 E 盘 + 另一物理介质 + 网盘） | 低（约 30 分钟 + ~200 MB，p10/p14 可选） | 这是唯一无替代来源的东西；上次的丢失已经证明了脆弱性 |
| **P1** | 把 `NX500_v1.12.bin` 与 `slp_a2_restore_v119.bin` 各存两处，并记录 md5 | 极低（336 MB ×2） | 三星 2016 年的下载页随时会没 |
| P2 | `st firmware up` 实刷一次（L2 通路验证） | 中（要重启） | L1 已验证，L2 只在"菜单进不去"时才用得上 |
| P3 | 研究 `dnloader.bin`（改造常规 bootloader） | 高 | 社区空白，但它才是 L3 的唯一门 |

**P0 的具体清单**（只读，`dd` 到 SD 再取回）：

```
boot0(4M) boot1(4M) p1(20M) p2(10M) p3(30M) p5(20M)
p6(10M) p7(30M) p8(50M) p9(150M) p11(100M) p12(5M) p13(10M)
p10(1024M) 可选——SLP 已覆盖，但自己有一份更安心
p14(2.3G)  可选——mod 本体，重建成本高但可重建
```

判据：每个文件 `md5sum` 双侧一致；`df` 确认 SD 余量 > 4 GB。

---

## 6. 工具

`raw8/fw/slp_tool.py`（本机 WSL python3，纯标准库）：

```
python3 slp_tool.py list    <slp>
python3 slp_tool.py verify  <slp>                     # 只读，逐段 JAMCRC + 平铺自检
python3 slp_tool.py extract <slp> <outdir>            # 拆段 + meta.json
python3 slp_tool.py repack  <indir> <outfile>         # 重建（自证用）
python3 slp_tool.py patch   <in> <out> --img N --at OFF --hex XX
python3 slp_tool.py bump    <in> <out> --version 1.19
```

- 唯一校验 = 每段独立 JAMCRC（`zlib.crc32 ^ 0xFFFFFFFF`），**容器无签名**
- `patch` / `bump` 写 `<out>.tmp` 后 rename，不改输入
- 自制包要能刷，**必须 `bump` 版本号**（同版本 UI 判灰）

---

## 7. 未闭合

| # | 项 | 说明 |
|---|---|---|
| 1 | p1/p2/p3/p5 的原始内容 | 无备份、SLP 不含、`dnloader` 无料 ⇒ 当前无任何来源 |
| 2 | L2 `st firmware up` 是否真能刷 | 静态看跳过 CRC，未实刷 |
| 3 | `dnloader.bin` 能否由常规 bootloader 改造 | 社区评价"可行但没人做过"；NX30 的不能借用（DRIMeIV ≠ DRIMeVs） |
| 4 | 更高版本官方固件（1.13/1.15）是否还能获取 | 未查；若拿到，可交叉比对段表差异 |
