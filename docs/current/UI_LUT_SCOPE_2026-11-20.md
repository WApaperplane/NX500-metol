# LUT 域切换：vlog 表不单独开页签（2026-11-20）

> 用户定案：「vlog LUT 不要单独多开一个页签了，在 LUT 页签上再单击切换就行了。」
> 本文固定三件事：交互怎么做、数据怎么走、哪些边界不能碰。

## 1. 为什么不是第三个页签

vlog 表和普通表**写的是同一个 A18 槽**，区别只在「这张表是按哪种输入域做的」：

- 普通表吃 8bit sRGB 素材，`lut.sh list` 列 `/mnt/mmc/looks/`
- vlog 表吃对数域素材，放在 `/mnt/mmc/looks/_vlog/`

所以它是**同一层的两本清单**，不是第二层。开第三个页签有两个具体代价：
720px 顶栏被挤薄（现在是 152+132+124+126+126+间距=696），以及「换层（LUT⇄FilmLab）」
和「换域（std⇄vlog）」两种语义混在同一排按钮里。

## 2. 交互

```
顶栏： ● LUT          FILMLAB │ 混合 │ 菜单 │ 关闭      ← std 域
       ● LUT·VLOG     FILMLAB │ 混合 │ 菜单 │ 关闭      ← vlog 域
```

| 操作 | 结果 |
|---|---|
| 不在 LUT 层时点 LUT 页签 | 换到 LUT 层（域不变） |
| **已在 LUT 层时点 LUT 页签** | **换域：std ⇄ vlog** |
| 点 FILMLAB | 换到 FilmLab 层（域保持，切回来还在原域） |

- 当前层的页签必须**仍是按钮**：`nk_label_colored` 没有命中区，「再点一次」会落空。
  选中观感靠把强调色临时塞进 `style.button.text_normal/hover/active` 再还原。
- 宽度对调（LUT 132→152、FILMLAB 152→132）：`● LUT·VLOG` 在 132 里减掉两侧
  padding 只剩 116，正卡边缘；FILMLAB 8 字符在 132 里仍有富余。总宽 284 不变。
- 状态行给一句说明（「V-Log 表 · 按对数域输入做的，配 V-Log 素材」），
  因为页签上的 `·VLOG` 只回答了「在哪」，不回答「这意味着什么」。

## 3. 数据通路

```
nxui.sh lut
  ├─ lut.sh list          → /tmp/nxui-look.rec      (std)
  └─ lut.sh list --vlog   → /tmp/nxui-look-vlog.rec (vlog, 列 looks/_vlog)
        ↓
nxui.arm --lut-recipes … --lut-recipes-vlog …
        ↓ 点页签换域
nx_load_recipes(另一份清单) → layers[L_LUT]
        ↓ 选中
lut.sh apply <name> → table_of(): looks/<n>.bin → 找不到再看 looks/_vlog/<n>.bin
```

- **两份清单各记各的选中项**（`scope_key[2]`），换回来自动停在原来的 look 上。
- **不预读 vlog 清单**：面板首帧实测 355 ms（菜单版 1151 ms），每一毫秒都算。
  第一次点页签才读，10 条约 4 ms。
- **`table_of` 回退 `_vlog/`**：名字全库唯一（导入时 `vlog_` 前缀 / `_65` 后缀区分），
  所以 `apply` / `mix` / `strength` 不用各加一个 `--vlog` 参数。面板换域只是换了
  一份清单，落到引擎侧就是「这个名字在 `_vlog` 里」。

### ★ 换域必须作废队列序号

队列（`nx_queue`）是按**数组下标**记账的。换域后同一个下标指向另一张表的另一项：
不清空的话，一个还没派发的选择会拿 10 项的 vlog 序号去 apply 120 项的 std 表
（或者反过来），写进去的是完全不同的表。所以 `toggle_lut_scope()` 里：

```c
L->q.desired = L->q.applied = -1;   /* 没有待发写入，当前选中也「未落地」 */
```

同时 `mix_a/mix_b = -1` + 退出混合态：A/B 也是下标，跨域混一张 std 一张 vlog 没有意义。

## 4. 边界

- **vlog 域禁用编辑态**（长按 → 删除/左移/右移）：`lut.sh del|move` 只认标准库的
  `order` 文件，vlog 表不进它。进去只会失败，所以直接给一句「用命令行 lut.sh」。
- **子进程在跑时不允许换域**：`busy_recipe` 是下标，重载清单会让它指到别的项
  （`poll_child` 里还要用 `bl->rec[busy_recipe].label` 报状态）。给一句
  「正在应用 · 完成后再切范围」，比静默指错安全。
- 换域**不改任何相机状态**：不写 A18、不触发 DMA。真机实测点击两次后
  `BACKEND kind=1` 计数为 0。

## 5. 顺带修的三个既有缺陷（8a12fe7「3×4 网格」之后漂移的）

| 文件 | 缺陷 | 后果 |
|---|---|---|
| `nxui.c::services_visible()` | 仍写 `page*6 … +6`（2×3 时代的 6 项/页） | **第 7 项之后的服务开关永远不刷新状态**（面板上的 Telnet/FTP 标签卡住）。改为 `MENU_PAGE`（3×4=12），翻页算式同步 |
| `verify_display.py` / `test_layers_2b.py` | 断言菜单页 `panel=720x336` | 网格改造后是 292，全套验证第一步就假失败 |
| 六个测试文件 | 写死 `out/test_input`（宿主版） | qemu/arm 目标下宿主二进制去加载相机 rootfs 的 ARM libX11，直接 exit 1 ⇒ **arm 的点击测试从来没真正跑过**。改为读 `NXUI_TEST_INPUT`，`verify_display` 为 arm 传 `test_input.arm` |

另外 `load_menu()` 失败时现在打一行 `MENU FAIL path|parse …`：之前它只改面板上的中文
状态，测试与现场排障都只看 stderr，于是「子菜单生成成功但没切过去」变成静默失败。
同因还修了 `test_interaction.py` 的子菜单数据 —— 菜单里 `@…/gui_ini.NX500` 这类
「返回」项自 8a12fe7 起不再进网格，用它当唯一项的测试菜单会被解析成 0 项。

## 6. 验证

| 项 | 结果 |
|---|---|
| 主机编译（gcc -Wall -Wextra）+ test_model / test_services | 通过 |
| `verify_display.py` host（含新增的换域点击断言） | 11 项全 PASS |
| `verify_display.py` arm（qemu + 相机 ABI） | 10 项全 PASS |
| 真机渲染 `● LUT` / `● LUT·VLOG` | 720×208，页签不裁切、中文正常 |
| 真机点击页签两次 | `SCOPE vlog entries=10` → `SCOPE std entries=120 sel=9` |
| 真机点击期间 `BACKEND kind=1` | 0 次（换域不写 A18） |
| 部署 | 3 文件 × 三处 md5 全等；vlog 10 张表上卡 `_vlog/`，逐文件 md5 一致 |

复现（真机）：

```bash
sh /opt/usr/nx-ks/nxui.sh lut          # 生成两份清单
killall -TERM nxui.arm
export DISPLAY=:0
/opt/usr/nx-ks/nxui/nxui.arm --root /opt/usr/nx-ks \
  --recipes /mnt/mmc/filmlab/recipes.txt \
  --lut-recipes /tmp/nxui-look.rec --lut-recipes-vlog /tmp/nxui-look-vlog.rec \
  --lut --snapshot /tmp/t.ppm
NXUI_LUT_SCOPE=vlog … --snapshot /tmp/t2.ppm    # 直接以 vlog 域截图（调试口子）
```

## 7. 遗留

- `lut.sh` 这次带了上午那份 P0 修复（`begin` 不查 `off` 退出码就备份 A18）。
  **`gamma/gamma-reload.sh` 的 `ENGINE_SHA` 锁着 lut.sh**，脚本一改哈希链就断 ——
  V-Log 引擎那条路会报身份不符。按 17:00 的结论（RC5 gamma 引擎退役、改用
  `bake_log.py` 把 log 烘进 3D LUT）这不影响主线，但菜单里「V-Log 曲线」按钮
  现在点了会失败，属于已知状态。
- vlog 表的 apply 没在真机上实做过（要写 A18，属相机状态变更，留给用户自己按）。
  代码路径是 `table_of` 三行回退，qemu 侧走的是同一支脚本。
