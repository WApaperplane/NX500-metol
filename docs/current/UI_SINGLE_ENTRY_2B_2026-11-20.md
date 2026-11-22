# 方案 2b：EV+WiFi 单入口 + 面板扩容（2026-11-20）

> 用户定案：「选方案 2b：EV+WiFi 单入口 + 面板扩容。」
> 本文固定三件事：入口怎么收敛、面板怎么装两层、两层怎么排先后与怎么归因。

## 1. 入口

| 键 | 去向 |
|---|---|
| **EV+WiFi** | `nxui.sh lut` —— **直达 LUT 滤镜面板**（唯一滤镜入口） |
| EV+AEL | 腾空，`exec EV_AEL.community.sh`（社区原版 AEL 行为） |

> ★ 2026-11-20 修正：2b 第一次落地时把「单入口」做成了「先给一页菜单」
> （`nxui.sh menu gui_evwifi.NX500`）。用户明确否掉：**要直接出 LUT 页**。
> 菜单页那一步是多按一次，而且和"单入口"的直觉相反。
> 现在 `gui_evwifi.NX500` 仍然保留（脚本/文档可引用），但**不再由按键进入**。

进面板后：

```
WiFi 键 / AEL 键  → LUT ⇄ FilmLab 换层（进程内，不重启、不黑屏）
顶栏「混合」       → 双表混合（仅 LUT 层）
顶栏「菜单」       → gui_ini.NX500（主菜单：实用功能 / V-Log 都在那儿）
顶栏「关闭」/ MENU / BACK / 半按快门 → 退出
同槽位再按 EV+WiFi → 关（nxui.sh 的 toggle 语义）
```

`filter:lut` / `filter:pw` 仍是 nxui 认的伪命令（`gui_func.NX500` 的
「LUT 滤镜」用它，`gui_evwifi.NX500` 也还写着），只是不再是主入口。

**为什么不用 `nxui.sh switch`**：它先 `killall -TERM nxui.arm` 再起新进程，
面板要黑 1 秒；更根本的是它假设「一次只该有一个面板」，而单入口模型下面板
必须活得比「当前在看哪一层」更久。进程内换层顺带消灭了 11-20 记下的
「面板完全 map 前连按会各起一个」的竞态。

## 2. 面板

```
┌──────────────────────────────────────────────────────────┐
│ ● LUT │ FILMLAB(按钮) │ 混合 │ 菜单 │ 关闭                │  ← 24px
├──────────────────────────────────────────────────────────┤
│  ● Look 00 │ Look 01 │ Look 02 │ Look 03 │ Look 04       │  ← 60px，5 项
│  1/79  R108 G100 B93  H11 S6 锐9 对比8                    │  ← 16px（仅 PW 层）
│  ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬                               │  ← 16px 滚动条
│  ‹ 上一个 │ 保存并关闭 │ 还原(原厂) │ 下一个 ›             │  ← 26px
│  演示模式 · 未连接相机                                     │  ← 23px 状态
└──────────────────────────────────────────────────────────┘  720×208
```

- **尺寸没变**：页签占用原来标题那一行，208 高度一字未动。菜单页仍是 720×336。
- **当前层的标记**：`● LUT` 用强调色（203,233,153）居中标签，另一层是普通按钮。
  没用 `nk_selectable_label` —— 它的选中态只换文字色，深色皮肤上两个页签长得一样
  （实测截图证实），等于没有标记。
- **两层各一份状态**：`struct nx_layer { rec[], n, sel, path, eng, q }`。列表、引擎、
  上次停在哪一项、应用队列全部按层独立。当前层由 `cur` 指针表示，老代码里的
  `recipes / count / selected / engine / queue / lut_mode` 变成指向它的宏，
  所以 draw 里那些 `lut_mode ? ... : ...` 的分支不用重写。
- **`busy_layer`**：子进程属于「启动它的那一层」，不是「现在看的那一层」。
  跑的时候用户可能已经换页签，`recipes[busy_recipe]` 会指到另一张表；
  `preview_status()` / `poll_child()` 一律按 `layers[busy_layer]` 记账，
  否则另一层的 `q.running` 永远不清零，那边的写入从此再也派发不出去。
- **换层不丢写入**：`set_layer()` 把旧层 `q.due` 置 0，主循环 `dispatch_pending()`
  按层扫一遍队列，先到期的先发。同一时刻仍只跑一个子进程 —— 两个引擎都在写
  相机状态，并发只会让归因更难。

## 3. 应用顺序（能定的与不能定的）

**定不了的部分**：三者在 ISP 里各占一个处理级，串行叠加是固件既定的，
软件改不了顺序。本文不发明硬件顺序。

**定得了的部分**（操作顺序）：

1. **V-Log 是地基，不是调味**。开了 log 之后 LUT 与 PW 的判断基准全变。
   另外 `gamma-reload.sh` 的门控要求 `LUT_IDLE`（preflight 七项之一），
   所以 V-Log 开关与 LUT apply **天然互斥**，必须分两步走，不能同时。
2. **先 LUT 再 PW**。LUT 一次 apply 要写 19,712 B 槽 + 触发一次 DMA，450 ms 防抖、
   约一帧过渡；PW 只是 7 个标量直推，150 ms 跟手。在 LUT 还没落地时调 PW，
   等于拿错的基准做微调。
3. **V-Log 开着的时候不用 PW 判色**。11-20 已记：FilmLab(PW) 与 V-Log(gamma)
   叠加后无法归因。

## 4. 归因策略（不用假读数）

面板**不显示**「这一层现在是否在机器上生效」——它不知道。LUT 的真实状态在 A18 槽里，
PW 的在 prefman 槽 9-12 里，面板读不到、也不该为了画一个灯去读。

归因靠**可控隔离**，这也是层存在的理由：

- 页签只回答「你现在在调哪一层」，不回答「这层在不在生效」。
- 想确认某层是否在起作用：切到该层，按它的还原键 —— LUT 是「还原原厂」
  （`lut.sh off`），FilmLab 是「还原」（`filmlab.sh neutral`，只直推 ISP 不写槽）。
  **另一层完全不动**，所以差异一定来自这一层。
- 换页签不改任何一层的相机状态，只有「选中某一项」才会应用。

## 5. 边界

- V-Log 不是页签：它是相机级状态，带门控、要等取景空闲，做成页签会给人
  「和 LUT 一样随时可点」的错觉。它留在菜单页的子页里（`@gui_vlog.NX500`）。
- `nxui.sh lut` / `nxui.sh filmlab` 仍可用（测试与其他脚本在用），语义降级为
  「打开面板并停在该层」，即 `--tab lut|pw`。
- `gui_func.NX500` 里的「LUT 滤镜」同步改成 `filter:lut`，否则从实用功能页点它
  会走 `nxui.sh lut` 的 toggle —— 那个分支见到 `pidof nxui.arm` 就 killall 再
  `exit 0`，等于把用户的面板关掉。

## 6. 验证

| 项 | 结果 |
|---|---|
| 主机编译（gcc，-Wall -Wextra） | 通过，仅有既存的 format-truncation 警告 |
| `test_model` / `test_services` | PASS |
| 三视图渲染 720×336 / 720×208 | 页签、列表、按钮行、状态行均无裁切 |
| `verify_display.py`（host） | 全绿：interaction / lut-interaction / library-ui / **layers-2b** / service / performance / focus |
| `verify_display.py`（arm，qemu + 相机 ABI） | 全绿，KEYMAP 24 项全解析 |
| ARM 交叉编译 | 472K，无 EFL 依赖，glibc ≤ 2.13 |
| 真机三视图 | `recipes=89`(LUT) / `79`(PW)，720×336 与 720×208 无裁切 |
| 真机 live 面板 | 首帧 1151 ms（font tier2 143 ms + map 777 ms），起停正常 |
| 部署 | 6 文件三处 md5 全等；SD 母本 185/185 无重复目录项 |

复现：

```bash
# 主机
bash build.sh host && NXUI_VERIFY_TARGET=host python3 verify_display.py
# 真机（★ 不要用 nxui.sh … --snapshot 连发：toggle 分支会互相 killall）
nxui.arm --root /opt/usr/nx-ks --recipes /mnt/mmc/filmlab/recipes.txt \
         --lut-recipes /tmp/nxui-look.rec --tab lut --snapshot /tmp/t.ppm
```
