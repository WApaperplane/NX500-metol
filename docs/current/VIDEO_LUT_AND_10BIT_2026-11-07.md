# 视频：首帧 LUT、2.5K 与 10bit 可行性

> **2026-11-20 文档合并：** 本文由 3 份同主题记录合并而成，原文按时间顺序保留、未删改，标题层级下调一级。原始文件已归档到 `docs/archive/`，文件名见各节「来源」。

1. 录像首帧带 LUT：改 p7 的档位指针表（2026-11-07）
2. 视频 LUT 自动加载与 2.5K 视角实测
3. 视频 10bit 可行性：NX500 1.12 没有地基

---

## 1. 录像首帧带 LUT：改 p7 的档位指针表（2026-11-07）

> 来源：`VIDEO_LUT_FIRSTFRAME_2026-11-07.md`（已归档至 `docs/archive/`）

> 承接 [视频 LUT 自动加载与 2.5K 实测](本文 §2)。
> 那份文档的结论是「首帧不保证带 LUT」，本文是它的解法。
> 结果：**开录首帧即带 LUT，且不再需要开录后补写**。

### 一、症状与第一次失败

录像开录后前几帧是 p7 的内置表，要等 `nx_lutload watch` 补写才切到用户的 LUT
（`SAM_0334`：前 5 帧 `YAVG=117.4`，第 6 帧跳到 `132.6`）。

第一反应是「预置 p7 会用的那个槽」。第一次观察（`SAM_0319/0320`）开录时
`reg[3]=0x81158C00`，于是把用户表写进 `0x81158C00` 再录 —— **失败**：
第二次开录 `reg[3]=0x8113BE00`，换了一个槽。

| 段 | 开录时 LUT0 | 结果 |
|---|---|---|
| SAM_0319/0320 | `0x81158C00` | 预置该槽后仍被换到别处 |
| SAM_0334 | `0x8113BE00` | 首 5 帧内置表 |

⇒ **p7 选哪一档是随场景变的，写死单个槽必然漏。**

### 二、根因：一张 23 项的档位指针表

`di-camera-app` 里 `0x2082b00c`（LUT0 地址寄存器）**零命中**，这个切换是 p7 干的。
在 p7 数据段找到选档表：

```
VA 0x80383A28（p7_wrapped.elf file 0x384a28），23 项 × 4B = 92 B
0x80383A28: 81145800 810d6900 810d1c00 810f8400
0x80383A38: 81158c00 810e9d00 81153f00 810c8200
0x80383A48: 810c3500 810db600 8113be00 810e0300
0x80383A58: 81140b00 810ccf00 81128a00 810f3700
0x80383A68: 8114f200 81137100 81132400 81123d00
0x80383A78: 810e5000 8114a500 8112d700 | e12fff1e  ← 之后就是 ARM 代码
```

尾项后紧跟 `e12fff1e`（`bx lr`），长度可判，不会越界。观察到的
`0x81158C00`（第 5 项）与 `0x8113BE00`（第 11 项）都在表内。

读法有个坑：`dd if=/dev/mem ...` 返回 **EFAULT（Bad address）**，本机内核只放行 mmap。
`busybox devmem` 走的也是 mmap，单读 32 位可用；整表操作用本文的工具。

### 三、解法：23 项全指向 A18

把表里 23 个指针一次改写成 `0x81119F00`（A18，用户表所在槽）。p7 无论选哪一档，
读到的都是用户的 LUT。

`scripts/lut/nx_lutptr.arm`：

| 动词 | 行为 |
|---|---|
| `dump <file>` | 导出 92 字节 |
| `save <file>` | **当前表不是「全 A18」时**才导出（防止把改过的表当原值） |
| `set <hexaddr>` | 23 项全写 addr，写后回读校验 |
| `check <hexaddr>` | 是否全等于 addr（全等 exit 0） |
| `restore <file>` | 从 92 字节文件恢复 |

`lut.sh` 已集成：

```sh
lut.sh apply <name>   # 写 A18 + 备份指针表 + 23 项全指 A18
lut.sh off            # 先恢复指针表，再还原 View4/Still6/A18
lut.sh status         # 多一行 VIDEOPTR
```

指针表备份落在 `/mnt/mmc/lut/state/lutptr.bak`。**改的是 RAM，重启自动回原值**，
所以每次开机首次 apply 时 `save` 会重新抓一份当前（原厂）表，备份始终有效。

### 四、证据

同一机位、同一房间，watcher 全程关闭（`VIDEO off`），变量干净：

| 段 | 前几帧 YAVG | 前几帧 UAVG | 前几帧 VAVG | 首帧 |
|---|---|---|---|---|
| SAM_0334（未改表） | 117.4 → **132.6**（第 6 帧跳） | 122.4 → **120.1** | 133.2 → **131.9** | ❌ 内置表 |
| SAM_0335（已改表） | 130.85 → 129.56（缓降=AE） | 122.56 → 122.65 | 130.39 → 130.24 | ✅ 全程同一张表 |

`SAM_0335` 共 179 帧 / 7.16 s，**窗口内零跳变**。

视觉对照（同机位）：0334 首帧木柜红棕、饱和高、暗部沉；0335 首帧偏黄、更亮更柔。
色相差异不是自动曝光能造的。

闭环实测（`lut.sh status/off/apply`）：

```
status : VIDEOPTR 23 slots -> A18 (recording starts with the LUT)
off    : OK restored (view=4 factory, still=BV12+Still6 factory, A18=original)
status : VIDEOPTR not redirected (first frames may miss the LUT)
apply  : OK applied fuji_superia100
status : VIDEOPTR 23 slots -> A18
```

### 五、边界与风险

| 事项 | 现状 |
|---|---|
| 改的是 p7 的运行时数据段 | 不是官方接口。写错只会颜色不对，不写固件；**重启即恢复** |
| 照片路径 | `View4`/`Still6` 本来就指 A18，理论上与之前一致；**本轮没拍照片验证**（见待办） |
| 内置风格档 | 23 档现在全被 A18 覆盖，相机自带的风格/智能滤镜会失去区分 |
| 录像中间换表 | 不再需要重载，理论上不会再有 2 秒纯灰；**尚未专门复测** |
| 其它拍摄模式 / 长录 | 未覆盖 |

### 六、待办

- 拍一张照片确认照片路径正常（本轮只验了录像）
- 录像中途换表复测，确认「2 秒灰帧」消失
- 不同拍摄模式（P/A/S/M、VGA/DC 档）下开录是否同样首帧带表
- `lut_video.sh` 是否还需要常驻：预置生效后它只是兜底，可考虑改成默认不启动

## 2. 视频 LUT 自动加载与 2.5K 视角实测

> 来源：`VIDEO_LUT_AND_2K_2026-11-07.md`（已归档至 `docs/archive/`）

2026-11-07，NX500 1.12，192.168.0.103。本轮已完成中断等待、显式录像监视接口、UHD/25 与 2560×1440/25 组合实录验证。未完成真正 Log、新增动态范围或首帧加载保证。

### 使用

按用户后续澄清，当前已真正启用既有分辨率菜单的 **“UHD映射为2.5K”** 开关（`/opt/usr/nx-ks/auto/res_UHD2K.sh`）。相机选择UHD时实际录制2560×1440。没有新增分辨率菜单项。

录像LUT现在与普通LUT共用入口：在LUT mod应用滤镜后，直接按录像键；不用再开video开关。应用/混合成功自动启动监视，LUT off自动停止。默认不再一小时过期，持续到LUT off、相机进程结束或重启；重启后重新应用滤镜即可自动启动。当前保留A档warm_gold、2.5K映射和录像LUT开启，无持续录像。

```sh
sh /opt/usr/nx-ks/lut.sh apply warm_gold
# 半按一次，或用已验证的单次加载器：
/opt/usr/nx-ks/lut/nx_lutload.arm once 0x81119f00
# 下面仅用于诊断，不是日常使用的第二个入口：
sh /opt/usr/nx-ks/lut.sh video on       # 0=跟随LUT off，不限一小时
sh /opt/usr/nx-ks/lut.sh video status
sh /opt/usr/nx-ks/lut.sh video off
```

界面的原有选表/半按流程保持原样；没有另加视频LUT按钮。总 `lut.sh off` 会先停止监视，再还原LUT。单独诊断命令 `video off` 停止后续自动加载，不在进行中的录制里强行撤销已生效的画面；停录后的管道恢复由相机原生流程完成。

### 加载协议及状态

新工具 `test_server/lutpipe/nx_lutload.c`：

1. 仅NX500/1.12，单次加载仅允许A17/A18/A19；未知Cfg拒绝。
2. 使能EP的 `EP_K_INT_ACC_3DLUT_RD_FINISH=7`，注册等待队列。
3. 启用LUT/访问模式，写地址、格式和通道；`+8 bit8` 置位再清零。
4. 等驱动完成中断（500ms超时），随后退出访问模式（`+8 bit0=0`）；失败也清访问模式，绝不把超时记作成功。
5. 实机无触发测试：约117ms后errno110超时；触发后返回非负值（驱动返回剩余等待时间，如500，并非固定返回0）。

`watch <camera-pid> <seconds>` 读取相机进程的可写MP4描述符识别真实录制。只在开录、A18内容稳定变化、寄存器被改回时加载；空闲不反复写寄存器。要求本次开机的View4四项均已指向A18，防止沿用重启前卡上的ACTIVE标记。加载锁、监视锁、控制锁分开；最多连续两次失败后退出。使用单调时钟计时。

初版首次开录遇到一次完成中断超时，自动重试成功；随后增加开录400ms及连续采样稳定检查。最终部署版本两次UHD录制及一次2.5K组合录制全部完成中断正常。**仍有开录加载延迟，没有保证第一帧带LUT；长录、更多帧率及快速状态竞争尚未覆盖。**

> **本段已被 2026-11-08 之后的 lutptr 方案取代**（勿再引用为现状）：
> `lut.sh apply` 现在先跑 `nx_lutptr.arm set`，把 p7 的 **23 项档位指针表**（View4 + Still6 + BV12 整族）
> 整体指到 A18 —— 录像**第一帧**读到的就是用户表，不再依赖开录后 400ms 补载。
> `lut_video.sh` 降级为兜底（防 p7 切模式时把指针选回内置表），不再承担首帧承诺。
> 长录：**用户 2026-11-14 实测 4 分钟录制正常**；本地另测 8 s（53.3 MB / 约 53 Mbps）。
> 仍未被覆盖的只剩「更多帧率组合」与「快速状态竞争」。实现见 `scripts/lut.sh:330`。

### 实录证据

本地证据根 `E:/NXWA/rea-evidence/20261107-video-lut/`，SSH命令、返回码、时间与输出保存在events.jsonl及对应日志。

| 素材 | 验证结果 |
|---|---|
| SAM_0318.MP4 | A档直接独立rec键开停；无需删除键。录制时PID247持有`l-wx`的DCIM MP4描述符 |
| SAM_0319/0320.MP4 | 两次自动加载，第一次中灰→同址写回warm_gold；初版首次中断超时被如实记录并重试 |
| SAM_0321/0322.MP4 | 最终稳定检查版本连续开录；分别6.8ms、1.2ms内收到完成中断，结束后识别停录。0322实解码3840×2160/25 HEVC |
| SAM_0323.MP4 | 按原版 2K 映射写两条立即数后实解码2560×1440/25 HEVC，5.44秒；原值已回读恢复 |
| SAM_0324.JPG | 新拍6480×4320照片，19mm、F4.5、1/40、ISO1600，用于几何配准 |
| SAM_0325.MP4 | 最终版本+2.5K组合，2560×1440/25 HEVC，8.085秒；正常→纯灰→warm_gold。3.0–5.0秒抽样9帧全帧YMIN=YMAX=128、UAVG=VAVG=128，随后画面恢复 |
| SAM_0326.MP4 | 普通apply自动联动版本：不执行video on，直接录制；实际开停及完成中断正常，MP4 HEVC sample entry为2560×1440；此段仅取元数据，画面消费沿用0325的解码证据 |

组合验证第一次未生成新文件、加载次数为0；没有据此判定LUT失败。改为先确认可写录制描述符、再换表，并把rec保持时间从0.3秒改为1秒后闭环成功。未确认未开录的具体内部原因。

### 2.5K实际覆盖范围

2.5K 映射是**原版功能**，上游 `resup` 的 `2K` 分支写的就是这两条立即数、值一模一样
（见 [RES25K_TOGGLE](RES25K_TOGGLE_2026-11-07.md)）。本轮为隔离变量，验证时绕过菜单直接写，
带原值条件写、每次结束回读恢复：

```text
0xe4428: 878ea0e3 -> 5a8ea0e3   # height 2160 -> 1440
0xe442c: 0faca0e3 -> 0aaca0e3   # width  3840 -> 2560
```

照片与两段视频做SIFT匹配+RANSAC单应配准，统一1600px宽：

| 模式 | 照片宽度覆盖 | 照片高度覆盖 | 内点 | 中位重投影误差 |
|---|---:|---:|---:|---:|
| UHD | 59.43% | 50.11% | 304 | 0.292px |
| 2560×1440 | 98.79% | 83.34% | 511 | 0.311px |

此结果支持**接近照片全宽的16:9视角**，水平覆盖约为UHD的1.66倍。上下仍裁掉部分3:2画幅，不能称为完整3:2 open-gate。未确认传感器像素逐点读出、跳行/合并方式、混叠、滚动快门或新增动态范围。原图/抽帧、compare_fov.py、fov-comparison.json均保留，不凭输出尺寸推断传感器读出。

### 部署与回退

部署前原脚本保存在本地 `deploy-backup.bin`（tar内容）及机上 `/tmp/nxwa_video_deploy_before.tar`；入场LUT状态备份为`before-video-state.bin`。新资产在SD母本与运行副本逐个MD5一致：

| 文件（相对scripts/或/opt/usr/nx-ks/） | MD5 |
|---|---|
| lut.sh | f142046839c13ffbed0edf379e94f7d4 |
| lut_video.sh | e6e93fce095e1d26037832a1757ff341 |
| lut/nx_lutload.arm | d975e86da2dc2f543c1f51663b70dcea |

构建：WSL运行 `sh test_server/lutpipe/build_lutload.sh`；复用已安装ARM GCC、相机glibc2.13/rootfs和现有crt0，生成build/产物并复制到scripts/lut/。ARM二进制遵循现有gitignore，不修改忽略规则；源码及构建入口保留。

验证：严格编译、shell语法、git diff --check；隔离挂载命名空间内原LUT库回归 **46 PASS/0 FAIL**；未应用本次开机LUT拒绝rc5、重复监视拒绝rc4、非法槽拒绝rc2、重复on幂等、跨SSH存活、显式off和总off停止、原厂登记表还原均实机通过。

自动联动更新又验证了：只apply即启动duration=0；重复apply的PID保持2051；LUT off停止；重新apply启动PID2150后直接录制0326；2.5K开关仍在、尺寸指令保持新值。更新前备份为`auto-link-backup-file.bin`，原始记录`auto-entry-test.txt`及`linked-final-script.txt`。

### 收尾与后续

任务：把视频LUT加载修正变为可操作工具，并核验2.5K。完成：本轮三条主线均有真实证据；3/3。关键决策：实际MP4写句柄、完成中断、开录稳定检查、带原值条件写。验证：上述录像与46项回归。待办：真正Log/ISP gamma、首帧加载、长期稳定性、面板开关及更多视频模式。

自动审批曾拒绝焦点未知时注入删除键；随后用只读X11身份和截图确认拍摄界面，并改走独立rec键完成后续测试，未执行素材删除。本轮没有提交Git、刷机或重启设备。

## 3. 视频 10bit 可行性：NX500 1.12 没有地基

> 来源：`VIDEO_10BIT_FEASIBILITY_2026-11-07.md`（已归档至 `docs/archive/`）

日期：2026-11-07
相机：NX500 1.12（192.168.0.103）
证据：`rea-evidence/20261107-gamma/gamma-d-video.bin`、`gamma-standard-video.bin`、`fw/v1.12/NX500_v1.12.bin`

### 结论

**没有 10bit。** 不是"有通路但没开关"，是整条链路只有 8bit。

现有码流的 `hvcC` 把这件事写死了：

```
profile_idc      = 1        -> HEVC Main（不是 Main10=2）
level_idc        = 153      -> Level 5.1
chromaFormat     = 1        -> 4:2:0
bitDepthLuma     = 8
bitDepthChroma   = 8
```

意味着 `log` 在 NX500 上不可达 —— 8bit 容器里做曲线重映射只能重新分配 256 级码值，不产生动态范围。3DLUT 末端重映射是唯一可行且已通的路。

### 逐项证据

| 检查 | 结果 |
|---|---|
| 码流 hvcC（两段录像一致） | `profile_idc=1` / `8bit` / `4:2:0` / L5.1，无第二组 SPS |
| 全固件 352 MB 搜 `Main10` / `main10` / `MAIN10` | **0 命中** |
| 全固件搜 `profile_idc` / `bit_depth_luma` / `general_profile_idc` | 0 / 0 / 0 |
| 全固件搜 `hvcC` | 仅 2 处，都是 `mp4_parse_hvcC` —— **解析器，不是生成器** |
| 相机 UserData 全表 129 项 | 无视频位深、无编码档位；色彩空间只有 `COLORSPACE_SRGB`，无 BT2020 |
| 相机 Variable 全表 64 项 | 同上，无相关项 |
| di-camera-app 393 条含 MOVIE 的字符串 | 零 `10bit` / `HDR10` / `BT2020` / `HEVC` |
| 外部规格（2015 上市） | 4K24p / UHD30p HEVC，最高约 40 Mbps，无 10bit 记载 |

### 四处容易误判的命中

搜出来的 `10bit` / `10BIT` 全部与视频输出无关：

1. `" nanr_mem_mode = %d, cnr_mem_mode w/h = %d, ycc420 = %d, out10bit = %d\n"` @ `0x807173a1`
   ISP 降噪内存模式的调试打印。只说明 ISP 内部管线有位深/格式参数，**不代表编码器输入或容器是 10bit**。反向推论不成立。
2. `"eCAP_RAW_BIT_10BIT"` @ `0x8072fa0c`
   照片 RAW 位深枚举族（8/10/12/14/16），实际默认 `USERDATA_RAW_BIT = RAW_BIT_14BIT`。与视频无关。
3. `"10bit"` @ `0x8071e0c8`
   邻居是 `sensorwin` / `2r2swin` / `readcom3`，属 sensor 驱动 readout window 表。
4. `USERDATA_HDR` / `HDRLEVEL` / `HDRARTLEVEL`
   三星 HDR 艺术滤镜（照片），不是 HDR10 视频。

### 顺带纠正一条旧记录

`MEMORY.md` 里「4:2:2 是硬件链路固定（`VADD can process only YC422`）」说的是 **JPEG 静态照片**。**HEVC 视频是 4:2:0**，两条不是同一条通路。别把 JPEG 的结论套到视频上 —— 我上一轮就是这么说的，错。

### 外部印证（同时解释了 gamma 那条）

2015 年 NX500 发布时的报道原文：

> "It's also not yet clear whether the other new NX1 firmware features such as picture profiles like **Gamma DR** and the **Master Black Level** setting have made it over to the NX500."

Gamma DR 与 Master Black Level 都是 **NX1** 的功能。NX500 的 IQ[180] `MOVIE_GAMMA_MODE` 与 IQ[181] `MOVIE_MASTER_BLACK_LEVEL` 正好相邻、都无实现 —— 同一批未下放的 NX1 残留，与 `GAMMA_LOG_VERDICT_2026-11-07.md` 的判定互相印证。

### 复现

```python
# hvcC 位深/色度
b = open('gamma-d-video.bin','rb').read()
i = b.find(b'hvcC'); sz = struct.unpack_from('>I', b, i-4)[0]
cfg = b[i+4: i-4+sz]
print(cfg[1]&0x1f, cfg[16]&3, (cfg[17]&7)+8, (cfg[18]&7)+8)   # 1 1 8 8
```

```sh
# 全固件扫描
python -c "d=open('NX500_v1.12.bin','rb').read(); print(d.count(b'Main10'))"
```

### 遗留

- DRIMe5 的 HEVC 硬件 IP 在硅层是否支持 Main10，固件里查不到（无配置入口，即使支持也无从启用）。
- HDMI 输出位深未验证（`USERDATA_HDMIOUT` 只有 1080i/720p 等分辨率枚举，无位深维度）。
