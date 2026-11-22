# NX500 板级射频：NX1 对照的结论与替换（2026-11-20）

> ⚠️ **2026-11-20 文档清理：本文第三节的「NX30 同腔体」类比对芯片层不成立。**
> NX30 的 Wi-Fi 是 Atheros AR6003（DRIMeIV），NX500 是 Marvell SD8887（DRIMeVs），不是同一颗；
> 仅「同一机身腔体装过双频天线」这一半保留。结论措辞与概率判断以
> [`WIFI_5GHZ_FINAL_2026-11-20.md`](WIFI_5GHZ_FINAL_2026-11-20.md) 为准。

> 结论先行：**NX1 不是好对照对象，NX30 才是。** 五条证据叠起来，5G 射频通路大概率物理存在，
> NX500 是"软件 mask + 只报 2.4G 认证"的双锁，不是硬件缺失。概率判断 70–80%，不给更高。
> 本轮零发射、零拆机、零上机。

## 一、NX1 内部照对照：做不成，原因已查清

FCC 公开件 A3LNX1 Internal Photos（342 KB / 6 页 9 图）与 A3LNX500（457 KB / 7 页 11 图）
都只拍**主板面**，两张里都没有任何 FPC 天线、同轴线或 ipex 座。

服务手册给了结构真相：

| 机型 | 天线模块位置 | 原文 |
|---|---|---|
| NX1 | **前盖下方** | "…and the antenna module. (You should remove the front cover to check the antenna module.)" |
| NX500 | **快门下方** | "…or connection with module (Antenna module is able to be checked after removal of shutter)" |

两机天线模块位置不同 ⇒ **不是同一件**，NX1 的天线结论不能搬给 NX500。
NX1 手册另一条（4-15 BT/Wi-Fi problems）也只说"检查 RF 天线装配"，无频段信息。

素材：`_deliverables/wifi5g/fcc/internal.pdf`、`nx1/internal.pdf`、`nx1_service.pdf`（58.8 MB，128 页）。

## 二、真正能对照的是 NX30

NX500 的官方定位是 **NX300 机身 + NX1 内脏**，天线腔体继承 NX300/NX30 一脉，
不是 NX1 的方块机身。而 NX30 的 FCC 记录把 5G 跑通了：

**FCC A3LNX30（2014）**

| 频段 | 类别 | 功率 |
|---|---|---|
| 2412–2462 | 15C DTS | 235 mW |
| 5180–5240 | 15E NII | 4 mW |
| 5260–5320 | 15E NII | 5 mW |
| 5500–5700 | 15E NII | 4 mW |
| 5745–5825 | 15C DTS | 68 mW |

注释原文：*"…supports 20MHz bandwidth mode in the 2.4GHz band and both 20MHz and 40MHz
bandwidth modes in the 5.7GHz band."*（无 80MHz ⇒ 11n 双频，非 ac）
NX1 手册 2-4 产品对比表亦写 NX30：**"Wi-Fi : IEEE 802.11b/g/n support Dual Band"**。

⇒ **同一个机身腔体，Samsung 已经跑通过 5GHz 收发。** 这是本轮最硬的一条。

## 三、五条证据叠加

| # | 层 | 事实 | 强度 |
|---|---|---|---|
| 1 | 硅片 | SD8887 是 1x1 **双频 11ac**；`fw_cap_info=0xff0b`，过滤前 `fw_bands=0x6f`（含 A/GAC/AAC） | 硬 |
| 2 | 固件 | 内建 39 条 × 17B 的 5G 功率表，与只读 GET 的 35 条**逐条全中** | 硬 |
| 3 | 同腔体机型 | NX30 双频 5G FCC 全四段认证（20/40MHz） | 硬 |
| 4 | 同平台机型 | NX1 11ac 5G 全四段 + 80MHz 认证 | 硬 |
| 5 | NX500 自身 | 只被 `dev_cap_mask=0xffffcbff` **单点**关闭；FCC 只报 2.4G | 硬 |

链条：芯片有 → 固件有表 → 同腔体跑通过 → 同平台跑通过 → NX500 只挡了一个软件位。

## 四、为什么不给 95%

1. **无法证明 NX500 的天线模块与 NX30 同件**。parts list 里两机都没有独立的 WiFi 天线件号
   （NX1 只有 `AD02-00350A CONDUCTIVE GASKET-GASKET_WIFI`，是接地衬垫不是天线），
   天线藏在 ASSY 里，料号无从对比。
2. **无法排除降本**：为 2.4G-only 省掉 5G 匹配支路（天线馈点旁的 π 网络少贴两个件）是常见做法，
   照片分辨率（FCC 884×663）看不出 0402 元件。
3. 固件功率表值域 8–15 **不是** dBm，与任一台机型的 FCC conducted 功率都不直接对应，
   不能据此判断"NX500 做过板级校准"。

## 五、下一步的两个选项

- **要确定**：只剩发射或拆机。拆机可只拆到快门（不必全拆），量天线模块辐射体长度与
  馈点匹配元件 —— 这是成本最低的物理判据。
- **要收益**：仍建议先做换到另一条 SSID（ch1）的 A/B（零风险，未做）。
  5G 即使通了，也只是把 1.3–2.2 MB/s 的瓶颈从"重传"换成"带宽"，边际 1.5–2×。

## 六、禁用命令（本轮新增）

`rdeeprom` 已在 22:11 使整机重启（`up` 归零），同族 `otpuserdata` / `memrdwr` / `regrdwr` 一并禁用。
重启后 dmesg 显示 `WLAN FW already running! Skip FW download` ⇒ **Linux 重启不重载 WiFi 固件**，
固件若被弄脏只能断电复位。
