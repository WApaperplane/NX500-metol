# Lumix V-Log LUT 导入：为什么要先烘一遍（2026-11-20）

上游：`changyun233/Lumix-V-log-LUTs`（107 cube + 25 `.vlt`），本次只取两组：

| 组 | 数量 | 说明 |
|---|---|---|
| `luts/varicam_official` | 35 | Panasonic 官方影视 look：Bleach Bypass / BandW 三档 / Hi-LoCon / Fashion / Sepia / Night 系 |
| `luts/lumix_color_lab_A/B/C` | 22 | Panasonic 色彩实验室（Hollywood Blue、Selen/Silky Chrome、Teal-Orange 等） |

没取的：`varicam_official_vlogto709` 与 `unknown_leica_like` 的 4 张（Classic/Natural
Rec709/Rec2020）是**纯转换表**，实测 0.42→0.418 —— 输出仍是 log 编码，烘完只会得到
灰片；作者胶片那批（changyun_kodaklike 15 等）与现有 121 张风格重叠，等链路验证完再议。

## 1. 为什么要烘：输入域错位

上游表的输入是 **V-Log 编码**（18% 灰 = 0.42、黑位 0.098 以上），而 NX500 的 A18
3D LUT 槽吃的是**相机 ISP 输出的显示参考流**（JPEG 域，前面没有对数解码）。
直接把 V-Log 表写进槽 = 双重错位：表以为素材很暗、实际很正常 ⇒ 欠曝 + 发灰。

前置复合解决这件事：

```
T_std(x) = T_vlog( S2V(x) )
```

`S2V` = 「Standard → V-Log」的前向转换（`_lutweb/vla/S5IIS2V.cube`，Panasonic S5II
官方标准↔V-Log 的拟合表），由 `_lutweb/vla/merge_standard_luts.py` 施加，
输出 33³ cube，再用 `tools/lutgen/cube_import.py` 转成 19712 B 的 17³ 槽表。

**注意这是"把 look 的原始意图搬到标准域"，不是"做一次中性转换"。** 实测 `kodaP400`：

| | 0.46 输入（sRGB 18% 灰） |
|---|---|
| 原 V-Log 表 | 0.585 |
| 烘成 Standard 后 | 0.509 |

18% 灰只提亮 11%，该 look 的意图保住了。但暗部仍会被抬一些（V-Log 表天然抬暗部：
0.18 输入 0.085 → 0.244），**观感上比原生 LUT 低对比一点，这是设计使然而非 bug**。

## 2. 命名

`merge_standard_luts.py` 只认 1–8 位 ASCII 字母数字（Panasonic 机内 LUT 名的限制），
所以烘焙阶段用 `v01..v35` / `l01..l22`，导入前复制成相机里能看懂的短名：

| 前缀 | 含义 |
|---|---|
| `vc_*` | VariCam 官方（`vc_bw_hicon`、`vc_bleach_sb`、`vc_sepia` …） |
| `lcl_*` | Lumix Color Lab（`lcl_hollyday`、`lcl_selenchr`、`lcl_tealor1` …） |

对照表在 `_luts/lumix_std_named/map.json`（不入库，脚本可重生成）。

## 3. 去重（ΔE00 > 3.112 才算新）

`cube_import.py` 只跟仓库 `looks/`（89 张）比，那会漏掉机上多出来的 32 张。所以另跑了
`check_distinct.py`，基准是整个库 125 张（looks 89 + `_luts/std` 31 + `_luts/vlog` 10）：

```
baseline 125 张 · 候选 57 张 · DISTINCT 56 / DUPLICATE 1
lcl_cinetone   kodak_ektar100   2.43  DUPLICATE
```

只有 `lcl_cinetone` 被挡（跟 `kodak_ektar100` 太像）。其余 56 张全部 DISTINCT，
最小间距 3.89（`lcl_cinebasea` vs `kodak_portra400_new`）。

顺带一个发现：`lcl_silkychr`（Silky Chrome）**原表就是纯灰度**（输入 (0.8,0.2,0.2)
输出 (0.218,0.218,0.218)），不是烘焙出错 —— 它是黑白 look，正好补上 4 张 VariCam
的 `vc_bw_*`。

## 4. 重跑

```bash
python3 tools/lutgen/pull_lumix_vlog.py            # 下载 + 烘焙 + 复制成可读名
python3 tools/lutgen/cube_import.py \
    --dir _luts/lumix_std_named --out _luts/lumix_bin
python3 tools/lutgen/check_distinct.py _luts/lumix_bin --markdown
```

三段都可重入：源 cube 存在就跳过下载，烘过的 cube 存在就跳过烘焙。

## 5. 上机

```bash
tar cf - -C _luts/lumix_bin . | ssh root@<cam> 'cd /mnt/mmc/looks && tar xf - && sync'
rm -f /tmp/nxui-look.rec     # 让 nxui.sh 重建清单（判据是 looks 目录 mtime）
```

目标目录是 `looks/`（**标准域**），不是 `_vlog/` —— 这批已经烘成 Standard 输入，
不依赖 V-Log gamma 引擎；引擎在 `$D/gamma/` 里默认没装，`_vlog/` 那 10 张仍是
"等引擎"的状态。

### 5b. 原表也进 `_vlog/`（2026-11-20 追加）

用户要求两条域都要。原 V-Log 表（不做 S2V）另转一份 17³ 槽表进 `looks/_vlog/`：

- 名字加 **`vl_`** 前缀（`vc_bleach_sb` → `vl_bleach_sb`）。不能与标准版同名：
  `lut.sh::table_of` 先查 `looks/`、查不到才查 `looks/_vlog/`，同名会永远命中
  标准版，log 版就再也拿不到。
- 去重要**只在同一域内做**。跟标准域那 125 张比会报 3 个"重复"（`vl_bw_locondk`
  ~ `mono_acros` 等），那是跨域伪重复 —— 同一张黑白表吃 log 和吃 JPEG 完全是
  两回事。只跟 `_vlog/` 现有 10 张比：**57 张全部 DISTINCT**（最小 3.36，
  `vl_agressive` vs `vlog_g200n`）。
- 上机后：标准域 176 条（120 + 56），V-Log 域 67 条（10 + 57）。

### 5c. 名字上限是 12 字符

面板列表 5 格、每格约 133 px，19px 字体下 **13 个字符就开始被裁**（实测
`lcl_nostbleach` 显示成 `lcl_nostblea`）。所以 `Nostalgic_BleachByPass` 缩成
`lcl_bleach`。批量导库前先按 12 字符过一遍名字。

## 6. 遗留

- 烘焙用的是 S5II 的 S2V。NX500 的 JPEG 编码不是 Panasonic Standard，两者白点与
  中间调标定不完全一致 —— 如果成片整体偏亮/偏冷，先怀疑这一段，而不是 look 本身。
- 56 张一次进库会让 `looks/` 从 121 涨到 177，面板 5 格滚动翻起来变长。要不要分批
  上、或者只留 `vc_bw_*` + Bleach Bypass 这几张强风格的，看实拍再定。
