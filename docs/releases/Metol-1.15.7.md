# Metol 1.15.7

发布日期：2026-11-20

这一版两件事：**vlog 表不再单独占页签**，以及一条把第三方 V-Log 创意表烘成可上机表的链路。

## 1. LUT 页签二次点击换域

顶栏只有 `● LUT` 和 `FILMLAB` 两个页签。vlog 表不占第三个 —— 它和普通表写的是同一个
A18 槽，只是「按哪种输入域做的」不同。

- FilmLab 层点 LUT → 回 LUT 层（域不变）
- **已在 LUT 层再点 LUT → 换域**：标准表 ⇄ V-Log 表，页签变 `● LUT·VLOG`
- 换域只重读一份清单，不写 A18、不触发 DMA；两个域各记各的选中项

标准表在 `looks/`，V-Log 表在 `looks/_vlog/`。

顺带：**`EV + AEL` 这版起是 V-Log 曲线入口**。V-Log 在菜单里埋得深（双击 EV → 实用功能 →
V-Log 曲线），而它是拍视频时最常切的开关之一；这个槽位自从 FilmLab 并进面板后就空着，正好给它。
语义与 EV+WiFi 一致：面板没开就开，已在跑就关。

## 2. Lumix V-Log 库

从 [`changyun233/Lumix-V-log-LUTs`](https://github.com/changyun233/Lumix-V-log-LUTs)
取了 `varicam_official` 35 张 + `lumix_color_lab` 22 张。

上游表吃 V-Log 编码（18% 灰 = 0.42），A18 槽吃的是相机 JPEG 域 —— 直接写会双重错位
（表以为素材很暗，实际很正常），结果是欠曝发灰。所以先做一次前置复合
`T_std(x) = T_vlog(S2V(x))`，再转成 17³ 槽表。

去重按 ΔE00（门槛 3.112），基准是整库 125 张：标准域 56/57 可用（挡掉 `lcl_cinetone`，
跟 `kodak_ektar100` 太像）；V-Log 域 57/57。

烘完的照片会比原生 LUT **略低对比**（0.18 输入从 0.085 抬到 0.244），这是 V-Log 表本身
抬暗部的特性搬过来了，不是缺陷。

## 3. 两个坑

- **名字上限 12 字符**。列表 5 格 × 133 px，13 字符起被裁（`lcl_nostbleach` 显示成
  `lcl_nostblea`），已缩成 `lcl_bleach`。
- **V-Log 域不是随时可用**。曲线引擎在机身 `$D/gamma/`，默认不随包安装；没有引擎时选
  vlog 表就是双重错位，请用标准域。

## 4. 顺带修掉

- `services_visible()` 还按 2×3 时代的 6 项/页算 —— 菜单第 7 项之后的服务开关永远不刷新。
- 验证脚本断言菜单页高度 336（实际 292），全套验证第一步就假失败。
- 六个测试写死宿主版 `test_input` —— arm 的点击测试从来没真正跑过。
- `load_menu()` 失败现在留 `MENU FAIL` 日志，不再是静默。

## 5. 致谢

- **[NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling)**
  —— 松下 V-Log ⇄ Standard 曲线的研究与拟合。没有这条曲线，107 张 V-Log 表在 NX500 上
  只能是双重错位的灰片，第 2 节的烘焙链路也就不成立。
- **[changyun233/Lumix-V-log-LUTs](https://github.com/changyun233/Lumix-V-log-LUTs)**
  —— 表的整理与预览（Booz / 尝云 / 翰博 / Lucas Hu / Lumix Color Lab / VariCam 官方）。
  **这些表不随包**，请从原链接获取。
- **[ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)**
  —— 血缘上游。
