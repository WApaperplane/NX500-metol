# NX500 5GHz：工程总结与停止决定（2026-11-20）

> **一句话**：5GHz 不是"没开的功能"，而是**驱动里一个位**关掉的；固件里 5G 功率表完整存在，
> 同平台/同腔体机型都通过 FCC 的 5G 认证。**射频通路存在与否，只有发射能回答**——本线到此为止。
> 全程零发射、零拆机、未改任何持久配置。

---

## 1. 起因：不是为了 5GHz，是为了传得快

起点是 Web 相册传原图慢。2026-11-20 实测（相机 192.168.0.108，PC 走 5GHz/ax 同 SSID）：

| 项 | 数值 | 排除了什么 |
|---|---|---|
| 单路 HTTP 原图 | 2.07–2.22 MB/s（≈17 Mbps） | — |
| 链路 phy / 信号 | 108 Mb/s、-60~-69 dBm | 应用层只拿到 **16%** |
| Tx excessive retries / Rx invalid frag | +3539 / +546 | **重传吃掉 84%** |
| SD 冷读 | 56.3 MB/s（diskstats 证实非缓存） | 存储不是瓶颈 |
| CPU 非 idle | 41–46% | 算力不是瓶颈 |
| 静态 vs 下载 CGI | 6.27 s vs 6.42 s（差 2%） | 与走哪个服务无关 |

根因是选错 SSID：相机连的那条在 ch6 / -69 dBm，ch6 上挤了 8 个 AP；同一台路由器还广播另一条
在 ch1 / -49 dBm，ch1 除自己外最强只有 -74 dBm。**换信道是 +20 dB 的零风险杠杆**，比 5GHz 便宜得多。

提速只有三条路：少传字节（1024 px 预览 8.8×，已落地）、改善射频（未做 A/B）、5GHz（本线）。

---

## 2. 已确证的事实

### 2.1 门在驱动默认值，不在固件

- 真机 `fw_cap_info=0xff0b` → 按 `mlan_fw.h` 的 `GET_FW_DEFAULT_BANDS` 算得 `fw_bands=0x6f`
  = B|G|**A**|GN|GAC|**AAC** ⇒ **固件宣告支持 5GHz 和 11ac**。
- `mlinux/moal_main.c:297` 把 `dev_cap_mask` 默认成 `0xffffcbff`（注释原文：
  `NX500 doesn't supports 5GHz and 802.11ac`），`0xff0b & 0xffffcbff = 0xcb0b` → `fw_bands=0x0b`
  = B|G|GN。真机 `mlanutl mlan0 bandcfg` 返回 `Infra Band=11 (B G GN)`，逐位吻合。
- 该默认值不只在源码里：`/lib/modules/sd8xxx.ko` 中 `dev_cap_mask` 符号处实测 `ff cb ff ff`。
- `moal_sta_cfg80211.c:5243` 始终注册 2.4G band，**只有 `fw_info.fw_bands` 含 `BAND_A` 才注册 5G**。

### 2.2 只读 GET 就能问固件，5G 功率表在里面

`woal_priv_hostcmd()`（`moal_eth_ioctl.c:315`）**只校验长度就转发 `MLAN_OID_MISC_HOST_CMD`，
没有任何频段/能力判断**。因此在默认的 2.4G-only 驱动上可直接发只读 GET，不必解锁。

真机结果（五组全部 `Result=0000`）：

| SubBand | 条目 | 信道 |
|---|---|---|
| 0x00 | 14 | 1–14 |
| 0x10 | 8 | 36,40,44,48,52,56,60,64 |
| 0x11 | 12 | 100…144 |
| 0x12 | **1** | 144（异常，预期 149–165 五条） |
| 0x13 | 14 | 183,184,185,187,188,189,192,196,7,8,11,12,16,34 |

条目结构：8 B 头（魔数 `0x0189`、长度、中心频、`0x14`）+ N×(rate, power)；
2.4G 每条 28 B（10 档）、5G 每条 38 B（15 档），与 `Size` 精确吻合。

### 2.3 两个固件镜像里都有这张表

`sd8887_uapsta.bin`(653112 B) 与 `sdio8887_sdio_combo.bin`(476196 B) 按**下载块**
（16 B 头 = `[类型=1][目标地址][长度][校验]` + 数据）还原后，得到**同一张 39 条 × 17 B = 663 B
的 5G 表**（SHA256 前 16 位 `a598f5eb8a1998a7` 相同）。

- 记录格式 = `1 B 信道 + 16 B 功率`，**没有计数字段**。
- GET 返回的是 `powers[1..15]`（后 15 字节，丢的是第一个）。四子带 35 条**逐条全中，0 条不符**。
- 表尾是 ch144，不是断裂；`0x84598`(→0x42bd0) 与 `0x849a8`(→0x42fcc) 两块目标地址差
  1020 = 1024−4，**存在 4 字节重叠**，后块覆盖——文件偏移不等于加载后内存布局。

### 2.4 FCC 记录：同平台与同腔体都认证过 5G

| FCC ID | 机型 | 频段 | 带宽 |
|---|---|---|---|
| A3LNX500 | NX500 | 2402–2480、2412–2462 | 11n 20 MHz |
| **A3LNX1** | NX1 | +5180–5240 / 5260–5320 / 5500–5700 / 5745–5825 | 11n/ac 20/40/**80** |
| **A3LNX30** | NX30 | +5180–5240 / 5260–5320 / 5500–5700 / 5745–5825 | 11n 20/40 |

NX1 与 NX500 同代同平台；NX30 与 NX500 **同机身腔体**（NX500 = NX300 机身 + NX1 内脏）。

---

## 3. 被推翻的假设（六次自我纠错，按时间）

| # | 原判断 | 推翻依据 |
|---|---|---|
| 1 | 「5G 功率表没随机型发布」（据主机侧 `txpwrlimit_cfg.bin` 只有 2G 段） | 只读 GET 直接问到固件内建表；主机侧覆盖文件 ≠ 固件默认表 |
| 2 | 「四层一致裁剪」（mask+功率表+FCC+前端） | 功率表这一层不成立，只剩两层 |
| 3 | 「uapsta 在 ch183 断裂，至少两个数据区」 | 把文件偏移当内存布局；按下载块映射后是单一连续表 |
| 4 | 「GET 取前 15 字节，第 16 字节被裁」 | 实际取 `powers[1..15]`；第 2 字节不是计数 |
| 5 | 「NX30 同腔体认证 5G ⇒ NX500 芯片级可比」 | NX500 源码树里有 `ath6k/AR6003/bdata-nx30.bin` ⇒ **NX30 是 Atheros AR6003（DRIMeIV）**，与 NX500 的 Marvell SD8887（DRIMeVs）不同芯片。只剩「同腔体装过双频天线」这一半成立 |
| 6 | 「band 实验能验证射频通路」 | band 只证驱动愿不愿注册；`moal_sta_cfg80211.c` 只看 `fw_bands`，而 `fw_bands` 正被 mask 卡死 ⇒ 结果几乎确定，信息增量极低 |

---

## 4. 未闭合

1. **板级射频**：天线模块带宽、馈点匹配是否含 5G 支路。任何静态手段都无法回答。
2. **sub2 为何只回 ch144**：需反汇编 `0x00fb` 处理函数（本机缺 capstone/objdump）。
3. **sub3 的 `StartFreq` 全报 4000**（厂商参考后 6 条为 5000），紧凑表无频基字段。
4. **`WlanCalData_ext.conf`**（1720 B 文本 → 550 B 载荷，作 `CFG_DATA type=2` 下发）字段语义未定，
   不能判断是否覆盖 5G。
5. **断电是否重载固件**：dmesg 见 `WLAN FW already running! Skip FW download` ⇒ Linux 重启不重载；
   真正断电后是否会 `download`，未验证。
6. **固件是否按机型裁剪 5G 子带**：静态表齐全，但运行时选择逻辑未知。

---

## 5. 决定停止的理由

| 维度 | 判断 |
|---|---|
| 收益 | 即使 5G 通了，瓶颈已从"带宽"变成"重传"；换 ch1 若到 8 MB/s，5G 边际只剩 1.5–2× |
| 代价 | `band` 需 SIGSTOP 三服务 + rmmod 三模块 + insmod 带 `dev_cap_mask=0xffffffff`；断网数十秒，失败需断电 |
| 不可逆项 | 天线若为 2.4G 窄带，5G 频段 VSWR 可能达 5:1，芯片无可靠 VSWR 保护 ⇒ PA 退化不可逆 |
| 工具约束 | 真机无 `iw`，`iwlist scan` 不能限定信道 ⇒ 会遍历 DFS 与扩展信道，做不到"只发 ch36–48" |
| 兜底 | 见 §7：最后一层救砖缺件 |

⇒ **不做**。要确定只剩发射或拆机，两者都不划算。

---

## 6. 现场纪律：新增禁用命令

`mlanutl mlan0 rdeeprom 0 16`（源码确认 `woal_read_eeprom` 只接受 `data_length==2`、
`action` 恒为 `MLAN_ACT_GET`、**无写路径**）在 22:11 使**整机重启**。

- 同属 `reg_mem` ioctl 家族的 `otpuserdata` / `memrdwr` / `regrdwr` 一并禁用。
- 恢复后一切正常：三模块齐、IP/MAC 不变、链路自动重连。
- 相机屏幕弹「功能不稳定」= 异常重启提示，非硬件故障。

---

## 7. 救砖现状（本轮查清）

维修手册有官方章节 **7-6 Main PCB Recovery**：拆后盖 → SD 卡根放 `dnloader.bin` + `nx500.bin`
→ 镊子短接 TP 点 → 上电 → LED 亮后松开 → 闪烁 2–3 分钟 → 完成，校准数据不丢。

| 层 | 手段 | 物料 | 状态 |
|---|---|---|---|
| L1 | 菜单刷 `nx500.bin`（`/mnt/mmc/<project>.bin`） | 官方固件 ✅ | 校验链已逆向（无签名，机型串 `strncmp`，只拦降级），**未实刷** |
| L2 | `st firmware up`（读 `/sdcard/<project>.bin`） | 官方固件 ✅ | 跳过 CRC 校验，**未实刷** |
| L3 | Main PCB Recovery | **`dnloader.bin` ✗** | 流程完整，**无料** |

- `dnloader.bin` 在 NX30/NX300/NX2000 的 OSS 包里有（`project/NX30/binary/dnloader.bin`），
  **NX1/NX500 的 OSS 包没有**（Samsung 未放，社区到 2025 年仍确认无人做出）。
- NX30 的不能借用：DRIMeIV ≠ DRIMeVs，dnloader 做 DDR/PMIC 初始化。
- 理论路径：拿 NX500 常规 bootloader 改造成 dnloader（社区评价"可行但没人做过"）。

---

## 8. 产物

| 文件 | 内容 |
|---|---|
| `docs/current/WIFI_5GHZ_ANTENNA_CASE_2026-11-20.md` | 板级射频证据链与概率判断 |
| `docs/current/WIFI_5GHZ_FINAL_2026-11-20.md` | 本文 |
| `_deliverables/wifi5g/PROBE-REVIEW.md` | 探测脚本审查、失败矩阵、四轮只读核查 |
| `_deliverables/wifi5g/POWER-ANALYSIS.txt` / `COMPACT-TABLE-CONCLUSION.txt` / `TABLE-LAYOUT.txt` | 功率表解码与两次纠错 |
| `_deliverables/wifi5g/PWRTABLE-RAW.txt` / `POWER-DECODED.json` | 只读 GET 原始响应与结构化解析 |
| `_deliverables/wifi5g/*.compact-5g.bin` | 两镜像还原出的 663 B 表 |
| `_deliverables/wifi5g/verify-blocks.py` / `extract-compact.cjs` / `analyze-power.cjs` | 复算脚本（均带断言） |
| `_deliverables/wifi5g/probe-5ghz.sh` | 未执行的探测脚本（dry-run 两次通过） |
| `_deliverables/wifi5g/fcc/` | FCC 内部照、iFixit 原图、NX1 服务手册（128 页） |

---

## 9. 若要继续，入口在哪

1. **零风险**：把相机换到同一路由器的另一条 SSID（ch1，比 ch6 高约 20 dB）做 A/B（还没做）。
2. **零风险**：拔电池冷启动一次，看 dmesg 是 `FW download` 还是 `Skip` —— 决定"固件是否可重载"。
3. **低风险**：`band`（不发射），确认驱动注册；代价是断网数十秒。
4. **需动手**：只拆到快门，量天线模块辐射体长度与馈点匹配件——成本最低的物理判据。
5. **不建议**：`scan` 全段（不可控信道 + 无校准功率 + PA 风险）。
