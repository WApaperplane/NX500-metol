# NXUI 交互细节：首帧、启动、面板与滚动

> **2026-11-20 文档合并：** 本文由 4 份同主题记录合并而成，原文按时间顺序保留、未删改，标题层级下调一级。原始文件已归档到 `docs/archive/`，文件名见各节「来源」。

1. NXUI 首帧：字体链实测与收敛（2026-10-19）
2. NXUI 面板打不开：busybox setsid 多套一层（2026-10-19）
3. NXUI 面板：还原按钮与"记住上次位置"（2026-10-19）
4. NXUI 横向滚动条（2026-10-19）

---

## 1. NXUI 首帧：字体链实测与收敛（2026-10-19）

> 来源：`NXUI_FIRST_FRAME_2026-10-19.md`（已归档至 `docs/archive/`）

> **一句话**：字体的钱已经赚完了 —— 原厂 10.7 MB 字体打开 147 ms，换成 GB2312 子集后 51 ms。
> 再往下裁（38 KB 面板子集）**一分钱不省**，因为 `FcFreeTypeQuery` 的固定开销与码点数几乎无关。
> 本轮把这条边界实测钉死，并修掉三个会让人误判的缺陷。

### 0. 结论前置

| # | 结论 | 证据 |
|---|---|---|
| 1 | 首帧 101–109 ms（冷 fontconfig 缓存），其中字体 50–54 ms 是最大单项 | `bench_font_tiers.sh`，qemu-arm + 相机 rootfs，3 次取最小 |
| 2 | **38 KB 面板子集没有收益**：按文件开 49 ms ≈ GB2312 family 51 ms | 同上 `opt0` 场景 |
| 3 | 68 个配方名里 **21 个含中文**，所以 FILMLAB 必须用 GB2312 档；只部署面板子集会一路降级到原厂字体，比不优化更慢 | `scripts/filmlab/recipes.json`；`num_non_ascii=21` |
| 4 | 缺字兜底必须走**按文件打开**：生产 `fonts.conf` 刻意不含 `/usr/share/fonts`，家族名在那里不存在 | `scripts/nxui/fonts.conf` |
| 5 | 本轮**没有性能提升**（101 vs 交付时的 122 ms 差异在 qemu 抖动内）；收益是把边界测清楚 + 修 3 个缺陷 | §5 |

### 1. 首帧成本构成

`INIT` 行（冷缓存，`bench_font_tiers.sh lut`，取 3 次最小）：

| 项 | ms | 说明 |
|---|---:|---|
| recipes_ms | 4–5 | 解析 68 条配方表 |
| display_ms | 6–7 | `XOpenDisplay` |
| **font_ms** | **50–54** | 打开 GB2312 子集并做覆盖校验 |
| backend_ms | 20–24 | `nk_xlib_init` |
| map_ms | 16–21 | `XMapRaised` + `XSync` |
| **FIRST_FRAME** | **101–109** | 进程启动到首帧可见 |

字体仍占一半。剩下的 50 ms 里，`FcFreeTypeQuery` 是固定大头（读 name/OS2/post 表、建 pattern），cmap 只占小头。

### 2. 字体候选实测

`bench_font_tiers.sh` 的 `font_ms`（qemu-arm，冷 fontconfig 缓存，3 次取最小）：

| 字体 | 字节 | 码点 | font_ms |
|---|---:|---:|---:|
| SDIC_GP_US（原厂） | 10,680,224 | 44,426 | 147（历史值）/ 165（本次 nomin 场景实测） |
| font-sub-gbk | 6,211,352 | — | 未测（无使用场景） |
| **font-sub-gb2312** | **1,864,592** | **7,547** | **50–54** |
| font-sub-panel | 38,356 | 256 | 49 |

> `fontprobe.arm` 在 qemu 下 `FcFreeTypeQuery` 返回 NULL（`bench_font_sizes.sh` 因此出空表），
> 所以这张表用 nxui 的 `INIT font_ms` 而不是探针的分项。探针的 `stderr` 输出已修
> （原来 `printf` 到 stdout 在 `__libc_start_main` 退出路径上不 flush，一行都不打），
> 真机上可以再用它做分项。

**关键**：38 KB 与 1.9 MB 的打开成本相同。裁到 256 码点只省 2 ms。

原因不是文件大小，也不是 cmap 遍历 —— 是 `FcFreeTypeQuery` 的**固定成本**（表解析 + pattern 构造）。cmap 从 7,547 降到 256 理论上省 ~25 ms，但实测没有，说明这条路径上还有别的固定项吃掉了它。

> 推论：想再省只能**绕开 fontconfig**（自建 pattern 直连 FreeType），那是另一条工程，收益上限约 40 ms，代价是失去字体匹配与缺字检测。当前不做。

### 3. 为什么面板子集不能当生产字体

除了 §2 的"没收益"，还有一个更硬的理由：

```
68 个配方名里 21 个含中文（EK Cyan 冷调 / TriX 400 暖调 / HP5 粗颗粒 / 暖调 棕褐 …）
```

`font_covers_panel()` 逐字检查**全部** label 与菜单项。只要有一条中文，面板子集就整档失效 → 降级。

按文件打开原厂字体是 370 ms。若当时只部署 38 KB 子集而不带 GB2312 中间档，**每次开 FILMLAB 面板都要 370 ms**，比优化前更慢。这是本轮最有价值的一条发现。

### 4. 最终字体链

```
默认（两级）
  tier1  NXUI_CJK（font-sub-gb2312.ttf，1.9 MB，family 打开）  51 ms   ← 常态
  tier2  /usr/share/fonts/SDIC_GP_US_20120720.ttf（按文件）    370 ms  ← 生僻字兜底

实验（显式 NXUI_FONT_MIN=<path> 才启用）
  tier0  面板子集（38 KB，按文件）                              49 ms   ← 已验证无收益，默认关
```

行为矩阵（`bench_font_tiers.sh`，冷缓存，3 次取最小）：

| 场景 | 档位 | font_ms | first_ms | 判定 |
|---|---|---:|---:|---|
| LUT 面板（英文 look 名） | 1 | 54 | 109 | 常态 |
| FILMLAB（68 条，21 条中文） | 1 | 50 | 103 | 常态 |
| look 名含 GB2312 汉字 | 1 | 51 | 101 | 常态 |
| look 名含 GB2312 以外（龍） | 2 | 49 | 251 | 罕见，可接受 |
| 原厂菜单（`--menu`） | 1 | 50 | 108 | 常态 |
| 二级 family 打不开 | 2 | 166 | 220 | 兜底仍有画面 |
| 显式开面板子集 | 0 | 49 | 100 | 与 tier1 相同 → 不启用 |

`READY` 行新增 `tier=N`，`INIT` 行新增 `font_total_ms`（进程启动到字体链确定的累计时间），缺字时日志给出具体码点：

```
FONT tier1=NXUI_CJK:pixelsize=19 (tier0 missing U+9F8D)
FONT tier2=/usr/share/fonts/SDIC_GP_US_20120720.ttf (tier1 missing U+9F8D)
```

### 5. 本轮修的三个缺陷

| # | 缺陷 | 影响 | 修法 |
|---|---|---|---|
| 1 | **降级后无条件再降一级**：`if (font_tier==0){…; font_tier=1;} if (font_tier==1){…}` —— 二级成功也会进三级 | 任何缺中文的面板都会白开一次 10.7 MB 字体（多付 150–170 ms），且日志看起来"二级也缺字" | 改用局部 `covered` 标志，只在真的缺字时才继续降级 |
| 2 | `XftCharExists` 在相机这套 Xft/libfontconfig 上对 CJK 返回 0 | 覆盖校验形同虚设，一路降级到原厂字体 | 改查 `XftFont->charset` + `FcCharSetHasChar`，纯本地、无 X 往返 |
| 3 | `nk_xfont_del()` 会无条件 `XftFontClose(font->ft)`，而 `ft` 为 NULL 时崩溃 | 字体打开失败的分支反而让面板崩掉 | 失败对象改 `free()`，只在 `ft` 有效时交给 Xft |

另修 `fontprobe.c` 的输出：进程通过 `__libc_start_main` 退出时不保证 stdio flush，`printf` 版本在 qemu 下**一行都不打**，导致之前的字体成本只能靠 nxui 的 `font_ms` 间接推断。改 `fprintf(stderr)` + `fflush`。

> 缺陷 1、2 都是本轮新增三级链时自己引入的；缺陷 3 与原 `fontprobe` 是既有问题。

### 6. 工具

| 脚本 | 作用 |
|---|---|
| `bench_font_sizes.sh` | 四个候选字体的字节/码点/query_ms 对照（fontprobe） |
| `bench_font_tiers.sh` | 七个场景的档位与首帧，冷缓存 × 3 取最小 |
| `bench_font_prod.sh` | 生产布局等价测试（GB2312 + 缺字回退），旧脚本，保留 |
| `bench_font_panel.sh` | 面板子集下界，一次性结论已并入本文 §2 |
| `build_fontprobe.sh` | 交叉编译 fontprobe.arm |

### 7. 回归（2026-10-19 17:50）

| 项 | 结果 |
|---|---|
| `build.sh`（host `test_model` + `test_services` + ARM 交叉） | PASS |
| `verify_display.py`（host + arm × filmlab/menu/idle/interaction/service-interaction/performance + focus-handoff） | 13/13 PASS |
| `test_regtabwrite.py` | 10 PASS（Windows；WSL 下按路径跳过） |
| `test_lut_sh_rotation.py` | 8 PASS |
| `test_cube2lut.py` | 14 PASS |
| `test_regtabdump.py` | 7 PASS（Windows） |
| GLIBC 上限 / NEEDED | 2.7 / `libX11 libXft libc librt`，与上版一致 |

产物：`scripts/nxui/nxui.arm` md5 `fcd8b1d4844104a39249627949561ec2`，456,132 B。

### 8. 还能往哪走（未做）

| 方向 | 上限收益 | 代价 |
|---|---:|---|
| 绕开 fontconfig 直连 FreeType 建 `XftFont` | ~40 ms | 自管字体匹配与缺字检测，跨固件风险 |
| 把首帧降到"先映射窗口再加载字体" | ~50 ms（但会先显示空面板） | 观感倒退，不可取 |
| 静态文本烘焙成位图 | ~10 ms | 只对固定文案有效，look 名仍要真字体 |
| `backend_ms`（Nuklear 初始化 20 ms） | ~5 ms | 收益小 |
| `map_ms`（X 服务器映射 16–21 ms） | ~10 ms | 受 X 服务器调度，不可控 |

结论：**字体这条线到此为止**。首帧 ~105 ms，其中 50 ms 是字体、20 ms 是 UI 后端、20 ms 是 X 映射、12 ms 是启动与解析。继续压需要换实现路径，不是调参能解决的。

## 2. NXUI 面板打不开：busybox setsid 多套一层（2026-10-19）

> 来源：`NXUI_LAUNCH_APPLET_2026-10-19.md`（已归档至 `docs/archive/`）

### 现象

机身上按 EV+WiFi（或主菜单「实用功能 → LUT 滤镜」）**完全没有反应**：屏幕不弹面板，
`/tmp/nxui.log` 只有一行，且不产生任何窗口。

### 根因

`scripts/nxui.sh` 末行：

```sh
$BB setsid "$BB" "$BIN" "$@" >> /tmp/nxui.log 2>&1 &
```

`busybox setsid` 的语义是 `setsid PROG ARGS`，只 exec **一个**程序。
多套一层 `$BB` 后，实际执行的是：

```
/opt/usr/nx-ks/busybox /opt/usr/nx-ks/nxui/nxui.arm ...
```

busybox 把 `nxui.arm` 当成 **applet 名**，直接报 `nxui.arm: applet not found` 退出。

正确写法：

```sh
$BB setsid "$BIN" "$@" >> /tmp/nxui.log 2>&1 &
```

### 引入点

旧版是 `$BB setsid "$BB" nice -n 15 "$BIN"` —— 中间那层 `nice` 是合法 applet，
它会 exec 后面的 `nxui.arm`，所以**一直是能跑的**。

10-19「字体链收敛」把 `nice -n 15` 删掉（nxui 自己设了绝对优先级 19），
但忘了同时去掉 `"$BB"` ⇒ 多出来的那层从"无害"变成"把二进制当 applet"。

该改动一直**未提交**，所以 `git diff` 里 HEAD 仍是带 `nice` 的正确版本。

### 为什么测试没抓到

`test_launcher.py` 的 busybox 桩把未知 applet 当作"直接执行"：

```sh
*) exec "$cmd" "$@" ;;
```

于是 `busybox nxui.arm` 在桩里照样执行了 nxui.arm，测试全绿。
真实 busybox 是报错退出，两者行为相反。

另外启动器是 `setsid ... &` 后立即 `exit 0`，原测试只断言 `returncode == 0`，
即使二进制没起来也永远为 0。

### 修复

1. `scripts/nxui.sh`：去掉多余的 `"$BB"`，并加注释说明为什么不能再套。
2. `test_server/nxui/test_launcher.py`：
   - busybox 桩的未知 applet 改成真实语义 —— 打印 `applet not found` 并 `exit 127`；
   - 补 `cksum` 分支（脚本里用到）；
   - 新增断言：日志里必须出现 `UI`（证明 nxui.arm 真被执行）。
   - 阴性对照已验：把该行改回 bug 版 → 测试报
     `AssertionError: 0 not greater than or equal to 1 : launcher must actually exec nxui.arm`。

### 上机验证（2026-10-19 19:0x）

```
pidof nxui.arm = 437
READY backend=nuklear-xft live=1 recipes=43 panel=720x192 nice=19 tier=1 sel=0
INIT recipes_ms=1 display_ms=25 font_ms=317 backend_ms=13 map_ms=18
FIRST_FRAME process_ms=517 launch_ms=955
BACKEND kind=6 ok=1 elapsed_ms=1392 output=
```

`nxui.sh available` → OK；面板正常渲染 43 个 look。

### 部署位置

| 路径 | md5 |
|---|---|
| `/opt/usr/nx-ks/nxui.sh` | `bb5b608b0934dacb59b57691385b2e8e` |
| `/mnt/mmc/scripts/nxui.sh`（SD 母本） | 同上 |

### 附带校准

10-19 部署只覆盖了 5 个核心文件，另有 12 个仍停留在 10-06 母本版本，
本轮一并推送到 `/opt/usr/nx-ks/` 与 `/mnt/mmc/scripts/`：
`EV_AEL.sh` `EV_EV.sh` `1HDR.sh` `gui_focus.NX500` `gui_ini.NX500` `loadgui.sh`
`menu.sh` `telnet_toggle.sh` `fstack.sh` `brb.sh` `nrec.sh` `zoomzoom.sh`。
机端备份在 `/opt/usr/nx-ks/nxui/backup-20261019c/`。

## 3. NXUI 面板：还原按钮与"记住上次位置"（2026-10-19）

> 来源：`NXUI_PANEL_UX_2026-10-19.md`（已归档至 `docs/archive/`）

> **一句话**：PW 面板新增「还原」，LUT 面板的「还原原厂」改为与 apply 同一条 sh 调用链，
> 两个模式都会记住上次停在哪一项（存名字，不存序号）。

### 0. 三个"还原"入口，语义不一样

| 入口 | 命令 | 写槽 | 落盘 | 重启后 | 切档后 |
|---|---|---|---|---|---|
| PW「还原」 | `filmlab.sh neutral` | 否 | 否 | 回原厂 | 回该档位的值 |
| LUT「还原原厂」 | `lut.sh off` | **是**（两槽 + 两张登记表） | 否（RAM-only） | 回镜像原厂 | 仍关（登记表已回 A17） |
| PW「保存并关闭」 | `filmlab.sh commit` | **是** | **是** | 保持配方 | 该档位即配方 |

这不是实现不一致，是两条通道本身的差异：

- **LUT** 的"关闭"能落进登记表，所以切档、半按都还是关着的；只有断电才回镜像。
- **PW** 的参数属于"当前档位"，面板固定用 CUSTOM1（slot 9）当画布（见 `filmlab.sh` 的 UI 快路径注释）。
  写中性进 slot 9 只是把画布刷成灰，并不会改用户正在用的档位；要让"还原"跨切档保持，
  必须写用户当前档位的槽 —— 那会破坏他真实的风格设置，所以不做。

所以 PW 的还原是**临时复位**，提示语写的是"已还原中性 · 切档或重启回原厂"。

### 1. PW「还原」

`scripts/filmlab.sh` 新增 `neutral` 动词：

```sh
sh /opt/usr/nx-ks/filmlab.sh neutral
```

- 中性值 = `R/G/B=100`、`HUE/SAT/SHARP/CON=10`（`set_r` 的换算里 100 → 2032，正是 1.0 增益）
  - ★ **2026-10-19 修正**：初版把这四维写成 0，实机按「还原」直接变黑白。原因是四维值域 0-20、
    **10 才是零点**（`VARIABLE_PW{HUE,SATURATION,SHARPNESS,CONTRAST}` 的 `raw16 = 16*(值-10)+15`）。
    发 0 = 负向拉满 ⇒ SAT 归零 ⇒ 黑白。R/G/B=100 的类比不适用于后四维。
- 走 `pw_direct 100 100 100 10 10 10 10 --yes`，即 A2 的 7 维直推 + 提交位
- 前提与 preview 相同：`FILMLAB_PW=1` 且 `pwsend.arm` 存在；不满足就 `REJECT`
- 不 `prefman save`、不写槽 ⇒ 0 次 eMMC 写入

面板侧：按钮回调把 `queue.applied` / `queue.desired` 置 -1，所以"还原"之后
"保存并关闭"会重新走完整 apply，而不是误判"已生效只保存"。

### 2. LUT「还原原厂」的调用方式修正

原来发的是 `bash -c "<lut.sh 路径> off"`，要求 `lut.sh` 带可执行位；
而 apply/preview 走的是 `sh <引擎> <动词>`，不需要。同一条链两种前提，
部署时很难判断"为什么预览能用、还原不能用"。

现在 `neutral`(kind 7) 与 `off`(kind 8) 都改成：

```c
execl("/bin/sh", "sh", engine, "neutral" /* 或 "off" */, (char *)0);
```

与 apply 完全一致。测试里 engine 桩没 chmod 也能跑，正是靠这个修正暴露出来的。

### 3. 记住上次位置

存的是 **key**（配方 slug / look 名），不是序号 —— 库增删条目后序号会指错项。

| 项 | 值 |
|---|---|
| 目录 | `NXUI_STATE_DIR`，由 `nxui.sh` export 为 `/mnt/mmc/nxui` |
| 文件 | `last-filmlab` / `last-lut`，内容一行 key |
| 写入 | 写 `.tmp` 再 `rename`，断电不会留半截文件 |
| 时机 | 面板退出时（点关闭、半按、Esc、超时都算） |
| 失败 | 目录不存在或不可写 ⇒ 静默跳过，下次从头开始 |
| 名字失效 | 库里没有这个 key ⇒ 回到第一项，不停在空项上 |

**默认关闭**：不设 `NXUI_STATE_DIR` 就不读不写。这样测试、demo、`--snapshot` 天然隔离 ——
之前把默认值写成 `/mnt/mmc/nxui` 时，`verify_display` 的 idle 用例把位置写进了共享目录，
下一次 interaction 就从第 4 项开始，测试红得莫名其妙。

`READY` 行新增 `sel=N`，可以直接看恢复到第几项。

### 4. 按钮布局（测试与维护的依据）

面板 720×**208**（10-19 晚加横向滚动条后由 192 加高，见 本文 §4）。

| 模式 | 行 | y | 列宽 / 列 |
|---|---|---:|---|
| PW | 滚动条 | 123..139 | 整宽 696 |
| PW | 按钮 | 144..170 | 4×168：15..176 / 191..352 / 367..528 / 543..704 |
| LUT | 滚动条 | 102..118 | 整宽 696 |
| LUT | 按钮 | 123..149 | 4×168：同上四列 |

PW 按钮行内容：‹ 上一个 / **保存并关闭** / **还原** / 下一个 ›
LUT 按钮行内容：‹ 上一个 / **保存并关闭** / **还原原厂** / 下一个 ›

LUT 面板没有 PW 分量行（省 16px），但没有省滚动条 —— 两模式都有一行 16px 的条，
所以 LUT 的按钮行仍比 PW 高 21px。
两处坐标都有测试钉住，改布局时先看 `test_interaction.py` 里的注释。

### 5. 验证（2026-10-19 18:15）

| 项 | 结果 |
|---|---|
| `verify_display.py` | **17 项 PASS**（host+arm × filmlab/lut/menu/idle/interaction/lut-interaction/service-interaction/performance + focus-handoff） |
| `test_interaction`（PW） | 新增：点第三列「还原」→ 引擎收到 `neutral` |
| `run_lut_interaction`（新） | 点第二列「还原原厂」→ 引擎收到 `off` |
| `test_last_choice.py`（新） | 写盘 / 恢复 / 未知 key 回落到 0，三项全 PASS |
| `test_lut_sh_rotation` / `test_cube2lut` | 8 / 14 PASS |
| `build.sh` host 单测 | PASS |

产物：

| 文件 | md5 | 大小 |
|---|---|---:|
| `scripts/nxui/nxui.arm` | `8a7001d4e76335dce28545f31e6276f9` | 460,516 B |
| `scripts/filmlab.sh` | `48fe58a91bc2d919e48347d6991f81c7` | 59,542 B |
| `scripts/nxui.sh` | `b0acc066a18c0e9da7f48d81a86e74ba` | 4,516 B |

### 6. 上机验收

| 步 | 动作 | 判据 |
|---|---|---|
| 1 | 开 PW 面板，选一张配方 → 半按 | 画面变（既有行为） |
| 2 | 再开面板 → 点「还原」 | 画面回中性；`/tmp/nxui.log` 有 `BACKEND kind=7 ok=1` |
| 3 | 点「还原」后切一次拍摄模式 | 画面回配方效果（这是设计，不是 bug） |
| 4 | 开 LUT 面板 → 点「还原原厂」 | 取景器回原厂表；`lut.sh status` 显示 IDLE |
| 5 | 关面板 → 重开（同模式） | 停在刚才那一项，不是第一项 |
| 6 | 删掉一张 look 的 `.cube` 后重开 | 若上次选的正是它，回到第一项，不报错 |

步 3 与步 5 是这次的两个关键判据：前者验证 PW 还原的语义边界，后者验证位置记忆。

### 7. 上机结果（2026-10-19 18:05–18:12，真机 192.168.0.106）

#### 7.1 部署

| 目标 | 文件 | md5 核对 |
|---|---|---|
| `/opt/usr/nx-ks/nxui/nxui.arm` | nxui.arm | `8a7001d4…` ✓ |
| `/opt/usr/nx-ks/lut.sh` | lut.sh | `813b6d22…` ✓ |
| `/opt/usr/nx-ks/nxui.sh` | nxui.sh | `b0acc066…` ✓ |
| `/opt/usr/nx-ks/filmlab.sh` | filmlab.sh | `48fe58a9…` ✓ |
| `/mnt/mmc/lut/nx_regtabwrite.arm` | nx_regtabwrite.arm | `896de680…` ✓ |
| `/mnt/mmc/lut/cube2lut.arm` | cube2lut.arm | `582b12f0…` ✓（机上本来就是这版） |
| `/mnt/mmc/lut/nx_slotwrite.arm` | nx_slotwrite.arm | `ed170830…` ✓（同上） |
| `/mnt/mmc/lut/nx_regtabdump.arm` | nx_regtabdump.arm | `25365683…` ✓（**首轮漏传，后补**） |
| `/opt/usr/nx-ks/nxui/fonts/NXUI_CJK.ttf` | 字体子集 | `9736b5e5…` ✓ |
| `/opt/usr/nx-ks/nxui/fonts.conf` | fontconfig | `fca41133…` ✓ |

备份在 `/opt/usr/nx-ks/nxui/backup-lut-20261019/`（5 个 `.prev`）。
**机上没有 `/opt/usr/nx-ks/lut/`** —— LUT 工具在 `/mnt/mmc/lut/`，`lut.sh` 的 `pick_tool` 就落在那儿。

四个工具都要在 `/mnt/mmc/lut/` 齐全：`nx_regtabwrite.arm`（写登记表）、`nx_slotwrite.arm`（读写槽）、
`cube2lut.arm`（.cube → 19712 B 槽表）、`nx_regtabdump.arm`（只读 dump，诊断用）。
**首轮漏了 dump 工具** —— 它不在 `lut.sh` 的调用链上，所以 apply/off 全都能跑，只有排查时才发现缺。

`nx_regtabdump.arm` 实测输出（2026-10-19 18:38，off 之后的原厂状态）：

```
CTRL 0x80011620: 0xe59f1138 0xe59f2138 0xe59f3138 0xf5d1f0c0 [4/4]
VIEW4 @0x803837F0: 0x810fd100 0x81101e00 0x81106b00 0x81115200 [4/4]
STILL6 @0x80382C5C: 0x81119f00 0x81115200 0x8111ec00 0x8110b800 0x81106b00 0x81110500 [6/6]
BV12 @0x80382B00: 12/12
DUMP 16384 bytes ok
```

`VIEW4[3]` 回到 `0x81115200`（A17）、`STILL6` 全部原厂 —— 与 off 的预期一致。

#### 7.2 数据面（`lut_accept_20261019.sh`）—— ALL PASS

```
PASS 第 2 次换槽 a18 -> a19
PASS 第 3 次换槽 a19 -> a18
PASS 第 4 次换槽 a18 -> a19
PASS 第 5 次换槽 a19 -> a18
PASS view 回读 a18/a19（每次）
PASS still 回读 a18/a19（每次）
PASS 状态保留 active=a18     ← A4
PASS off 后状态 IDLE         ← A3
PASS view 已回 A17
PASS a18 已还原 / a19 已还原
ALL PASS
```

脚本自身有三个 bug 导致第一轮误报 12 项，已修（`lut_accept_20261019.sh`）：

| # | bug | 现象 |
|---|---|---|
| 1 | `grep "READBACK.*$cur"` —— `$cur` 是 `a18`，日志里是 `0x81119f00` | 明明回读正确却报 "view 回读不是 a18" |
| 2 | `nx_slotwrite.arm` 硬编码 `/opt/usr/nx-ks/lut/` | §5 槽比对读不到文件，两槽都报"不一致" |
| 3 | `regtabwrite md5` 同样硬编码 | 环境段显示 `md5=?` |

现在两处都用 `find_tool`（先 `/opt/usr/nx-ks/lut`，再 `/mnt/mmc/lut`），A2 断言在地址层面比。

#### 7.3 真机首帧（`selftest.sh`，1.5 s 自退，不写状态）

| 模式 | tier | font_ms | backend | map | FIRST_FRAME |
|---|---|---:|---:|---:|---:|
| FILMLAB（68 条） | 1 | 324 | 7 | 22 | **592 ms** |
| LUT（43 个 look） | 1 | 351 | 10 | 19 | **540 ms** |

**真机比 qemu 慢 5–6 倍**（qemu 冷缓存 51 ms / 首帧 105 ms）。差距几乎全在字体：Cortex-A9 建 7,547 码点的 `FcCharSet` 要 300+ ms，x86 上的 qemu 只要 50 ms。
⇒ 之前按 qemu 数据定的"首帧 ~105 ms 是下限"只适用于模拟环境；真机下限在 500 ms 量级，且字体仍占六成。

> 这条也意味着：字体子集在真机上的收益**可能比 qemu 显示的更大**（原厂 44,426 码点在真机估计 1.5–2 s），但本轮没有做"原厂字体真机基线"，所以不写具体数字。

#### 7.4 待目视

数据面只证明写入与登记正确。下面四条必须人眼确认（见 §6）：

1. PW 面板选一张配方 → 半按 → 画面变
2. 面板点「还原」→ 画面回中性
3. 点「还原」后切一次拍摄档位 → 画面回配方（设计如此）
4. LUT 面板点「还原原厂」→ 取景器回原厂
5. 关面板再开 → 停在刚才那一项

## 4. NXUI 横向滚动条（2026-10-19）

> 来源：`NXUI_SCROLLBAR_2026-10-19.md`（已归档至 `docs/archive/`）

> **一句话**：两个模式都加了一条整宽的横向滚动条 —— 按住任意位置即跳过去，
> 拖动实时跟手；为此面板高度从 192 加到 208，并且**不能用 Nuklear 的 `nk_slider_float`**。

### 1. 为什么不用 `nk_slider_float`

Nuklear 的滑块只在「**当前**鼠标位置仍落在滑块矩形内」时才继续更新：

```c
/* nuklear.h, nk_slider_behavior → nk_input_has_mouse_click_down_in_rect */
left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, *visual_cursor, nk_true);
/* 里面最后一步是 nk_input_is_mouse_hovering_rect(i, r) —— 用鼠标当前位置，不是按下位置 */
```

本次轨道 696px、68 个配方时每步 10px 尚可，但界面只露出 5 项、`max=count-1`：
8 项的 LUT 列表每步 99px，而滑块只有 26px。手指一移出滑块，hover 判定立刻失败，
拖动在第二帧就断 —— 表现为**按住能高亮、一拖就停**，触摸屏上等于不可用。

实测（修复前）：`test_input drag 20 110 480 110 10` 发完后 `/tmp/nxui.log` 里
一条 `BAR` 都没有，引擎一次 `apply` 都没收到；`verify_display` 在
`run_lut_interaction` 红。

### 2. 实现

`scrollbar_row(ctx, sel, n)`：自己取矩形、自己算比例、自己画，返回目标序号或 -1。

| 项 | 值 |
|---|---|
| 行高 | 16px（触摸判定按整行，轨道视觉只有 8px 居中） |
| 轨道 | `x=12, w=696`，圆角 4，色 `45/53/59` |
| 已选段 | 从轨道左端到滑块中心，色 `88/111/79` |
| 滑块 | `30×12`，色 `203/233/153`（比滑块默认值大 4 倍，手指才按得住） |
| 映射 | `ratio = (mouse.x − track.x − 15) / (696 − 30)`，`want = round(ratio × (n−1))` |
| 按下 | 行内任意位置 ⇒ 立即跳到该比例（tap-to-jump），不需要先拖 |
| 抬手 | **单独补一帧计算**（见 §3） |

两个模式共用同一行控件，只是 y 不同。

### 3. ★ 抬手那一帧必须补算

面板渲染是 10 Hz（`next_frame` 节流），触摸板 20ms 一次的 motion 会被合并：
松手时鼠标早就在终点，但最后一帧处理到的可能是半路的位置。

修复前实测：拖到 x=480，最后只落到第 3 项（log 里最后一条 `BAR sel=0 want=3 x=296`）。
判据是「松手位置才是终点」，所以：

```c
was = bar_drag;
if (press && hover) bar_drag = 1;
if (bar_drag || (was && !down)) { …按 mouse.pos 重算… }
if (!down) bar_drag = 0;
```

`ButtonRelease` 到达前事件循环已经先喂过 `nk_input_motion(x2,y2)`，所以这一帧
`mouse.pos` 就是终点。修复后同一拖拽落到 `look05`（`(480−27)/666×7 = 4.76 → 5`），
与几何预期一致。

### 4. 布局代价：192 → 208

新增 16px 行 + 5px 行距。192 高时按钮行会被 Nuklear 静默丢弃
（10-19 早先已经在 84px 滤镜条上踩过同一个坑），所以面板加高到 208：

| 层 | y（PW） | y（LUT） |
|---|---|---|
| 标题行 24 | 8..32 | 8..32 |
| 滤镜条 60 | 37..97 | 37..97 |
| PW 分量行 16 | 102..118 | — |
| **滚动条 16** | **123..139** | **102..118** |
| 按钮行 26 | 144..170 | 123..149 |
| 底部状态 23 | 175..198 | 154..177 |

`resize_panel()`、`height = 208`、`Display must be at least 720x208` 三处同步；
`verify_display` 的快照断言也改成 `720 208`。

**按钮行整体下移 21px**，`test_interaction` 里 PW 的「保存并关闭」和「还原」
坐标、LUT 的「还原原厂」坐标都已按新行重钉（x 不变，y +21）。

### 5. 测试

| 用例 | 断言 |
|---|---|
| `test_input drag`（新） | `drag x1 y1 x2 y2 [steps]`：press → 10 次带 `Button1Mask` 的 MotionNotify → release。**只有 press/release 的 click 测不出 motion 链** |
| `run_lut_interaction` | 拖到 480px ⇒ 最后一条调用**必须正好是** `apply look05`（不是"变了就行"） |
| `run_lut_interaction` | 点轨道 680px ⇒ 必须 `apply look07`（tap-to-jump） |
| `verify_display` | 17 项：host/arm × filmlab/lut/menu/idle/interaction/lut-interaction/service-interaction/performance + focus-handoff，**全 PASS** |

阴性对照：把 `scrollbar_row()` 换回 `nk_slider_float`，`run_lut_interaction` 立刻红
（引擎收不到 apply，日志里没有 `BAR`）—— 这条测试确实钉住了拖动链路。

`/tmp/nxui.log` 新增诊断行（首帧一次 + 每次换项一次）：

```
BAR rect=12,102 696x16 st=2
BAR sel=0 want=3 x=342
BAR sel=3 want=5 x=480
```

### 6. 上机（2026-10-19 19:45，真机 192.168.0.106）

| 项 | 值 |
|---|---|
| `/opt/usr/nx-ks/nxui/nxui.arm` | `cedc8305dd5fa506ab3c978dd802d9b7` 460,628 B |
| `/mnt/mmc/scripts/nxui/nxui.arm`（SD 母本） | 同上 —— **必须同步**，否则引导器下次触发会打回旧版 |
| 备份 | `/opt/usr/nx-ks/nxui/backup-scrollbar-20261019/nxui.arm.prev` = `8a7001d4…` |
| 安装脚本 | `test_server/nxui/install_scrollbar.sh`（setsid 后台 + md5 三方互证） |

`selftest.sh` 机端输出：

```
READY backend=nuklear-xft live=1 recipes=68 panel=720x208 tier=1 sel=0
BAR rect=12,123 696x16 st=2          ← FILMLAB
FIRST_FRAME process_ms=526  (font_ms=314)
EXIT renders=2 elapsed_ms=1513

READY backend=nuklear-xft live=1 recipes=43 panel=720x208 tier=1 sel=0
BAR rect=12,102 696x16 st=2          ← LUT
FIRST_FRAME process_ms=449  (font_ms=293)
EXIT renders=2 elapsed_ms=1507
```

两模式 `rc=0`，行位置与宿主布局表逐像素一致。首帧与加滚动条前同量级
（526/449ms，字体仍占六成）—— 新增的一行绘制成本在噪声内。

### 7. 待现场目视

数据面证明不了手感，下面三条要手指确认：

1. LUT 面板按住条子从最左拖到最右 → 卡片跟着滚、松手停在第 43 个 look
2. 点条子中段 → 直接跳过去（不用先拖）
3. PW 面板同样两条；拖完半按 → 落的是松手时高亮的那张

若 **拖动不跟手但点击能跳**，说明相机触摸板不发按住状态的 motion —— 这是
降级而不是故障（§3 的抬手补算保证终点正确）。此时要补的是 `Button1MotionMask`
之外的触摸事件源（XInput2 或 `evdev` 直读）。
