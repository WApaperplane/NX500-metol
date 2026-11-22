# P0 实机执行清单（2026-11-20）

> 配套 [RUNTIME_P0_ACCEPTANCE_2026-11-16.md](RUNTIME_ARCH_AND_P0_2026-11-16.md)。那份定义「怎么算通过」，本文只回答「按什么顺序、敲什么、什么时候必须停」。
> 所有命令都在 PC 上通过 `tools/nx-ssh.sh` 下发；相机侧一律用 `/opt/usr/nx-ks/busybox` 绝对路径。
> 状态：**两个观测脚本已改写并在 PC 上通过模拟验证；A/B/C 三轮尚未执行。**
>
> **本文的立场**：这一阶段只回答一个问题——**现有守卫观察到了什么、漏掉了什么**。
> 不预设缺陷结论，不用时序推断代替实测。所有涉及“会不会误判”的判断，都必须先有采样证据。

## 0. 总则

```sh
# PC 侧统一前缀。相机 IP 是 DHCP，先确认
cd E:/NXWA/nx500-modding-work
export NXHOST=192.168.0.108
ssh_cmd() { bash tools/nx-ssh.sh cmd "$1"; }
```

四条硬护栏：

1. **A1 是破坏性实验**。转码进程起之前记一次 `MemFree+Cached`；低于 40960 KB 立即放弃该轮，不"试一试"。
2. **出现 camera daemon 消失** → 立即停录、记现场、终止当日测试。该现象已知需拔电池。
3. **每轮开始前确认卡上有空间**（`df`），录像 30 s @80 Mbps ≈ 300 MB。
4. **不格式化、不清理、不修文件系统**。测试只读既有素材，除 A1/A2 主动制造压力外不改相机状态。

## 1. 环境快照（每次开机后先跑一次）

```sh
ssh_cmd 'BB=/opt/usr/nx-ks/busybox
 echo "== uptime =="; $BB uptime
 echo "== cam =="; $BB pidof di-camera-app
 echo "== mem =="; $BB awk "/^MemTotal:|^MemFree:|^Cached:/" /proc/meminfo
 echo "== cap =="; st cap capmm show 2>&1 | grep -aE "Allocable|Used"
 echo "== sd =="; $BB df -k /mnt/mmc | $BB tail -1
 echo "== newest =="; $BB ls -t /mnt/mmc/DCIM/*/*.[JSM][PR][WG] 2>/dev/null | $BB head -3'
```

记入表格（每轮一行）：

| 开机时刻 | uptime | cam pid | MemFree | Cached | allocable | used | SD 余量 |
|---|---|---|---|---|---|---|---|

**判据**：`pidof di-camera-app` 非空；`MemFree + Cached ≥ 40960`；SD 余量 > 2 GB。
**不满足** → 不开测，先查为什么。

## 2. 采样脚本

已写入 `scripts/p0-sample.sh`，推到 `/tmp/nx-p0/`（tmpfs，不写 SD）。

```sh
bash tools/nx-ssh.sh cmd 'mkdir -p /tmp/nx-p0'
bash tools/nx-ssh.sh push scripts/p0-sample.sh /tmp/nx-p0
# 自检：8 秒空载，应看到 avg_ms 在 120–140 之间
ssh_cmd '/opt/usr/nx-ks/busybox sh /tmp/nx-p0/p0-sample.sh 8 /tmp/nx-p0/selftest.tsv | tail -3'
```

输出 TSV：`n  sec  pid  wfd  bytes  avail`

- `t_ms` = **逐样本**单调时间戳，取自 `/proc/uptime`（10 ms 精度，不 fork）。**不要用样本数 × 平均周期推算事件时刻**——平均周期不是时间戳
- `guard` = 原守卫正则 `l-w` 数到的行数，**仅供对照**，不代表 fd 打开模式
- `rw` = 真实「可写且指向 `DCIM/*.mp4`」的 fd 数，读 `/proc/<pid>/fdinfo/<fd>` 的 `flags`，低两位非 0 才算可写
- `fd` / `tgt` = 当前跟踪的 fd 号与目标文件名，**目标取自 FD 本身**，不按目录 mtime 猜
- `bytes` = 跟踪文件字节数；**仅在 fd 消失后**才开始累加稳定计数（录像期间大小暂时不变不等于写完）
- `note` = `REC`（进入录像）/ `TAIL`（fd 消失后观测写尾）/ `STABLE`（写尾阶段连续不增长）/ `NO_FD` / `NO_PID`

**周期**：实测约 130 ms/样本（相机上每次 busybox fork ≈15–20 ms）。周期只用于判断采样密度，**不用来换算事件时刻**；事件时刻直接读该行的 `t_ms`。

**失败即停**：自检若行数明显少于 `时长 ÷ 0.15`，或出现 `pid=NA` 抖动，先查相机负载，不带坏采样开测。

**PC 侧自检**（不接触相机）：

```sh
sh tools/test_p0_scripts.sh    # 模拟 REC/TAIL/STABLE 三段时间轴，12 项断言
```

## 3. P0-A 成片完整性（三轮）

三轮必须**同卡、同码率、同机位、同录制时长**（建议 30 s）。每轮之间间隔 ≥ 60 s 让内存回落。

### A0 空载基准

```sh
# 1) 开采样（后台 60 s），2) 期间手动按 REC，录 30 s，停录
ssh_cmd '/opt/usr/nx-ks/busybox sh /tmp/nx-p0/p0-sample.sh 60 /tmp/nx-p0/A0.tsv > /dev/null 2>&1 &
        /opt/usr/nx-ks/busybox renice 0 -p $! 2>/dev/null; echo started'
# 立刻手动操作：按 REC → 等 30 s → 停录 → 等 10 s
# 采样结束后取回
bash tools/nx-ssh.sh pull /tmp/nx-p0/A0.tsv E:/NXWA/_p0
```

### A1 破坏性：录像中强行触发 1024 px 冷转码

**前置**：先跑一次 `ssh_cmd` 确认 `MemFree+Cached`，低于 40960 直接跳过本轮。

```sh
# 先冻结一张待转码源（任意已有 JPG）
ssh_cmd 'BB=/opt/usr/nx-ks/busybox; $BB ls -t /mnt/mmc/DCIM/*/*.JPG | $BB head -1'
```

同一命令里做三件事：起采样 → 起转码 → 全程 1 s 粒度看内存。

```sh
ssh_cmd 'BB=/opt/usr/nx-ks/busybox
 SRC=$(BB=/opt/usr/nx-ks/busybox; $BB ls -t /mnt/mmc/DCIM/*/*.JPG | $BB head -1)
 echo "SRC=$SRC"
 $BB rm -f /tmp/nx/thumb/t1024.jpg
 $BB sh /tmp/nx-p0/p0-sample.sh 60 /tmp/nx-p0/A1.tsv >/dev/null 2>&1 &
 $BB renice 0 -p $! >/dev/null 2>&1
 # 等 10 s 后制造压力（此时请已按 REC）
 $BB sleep 10
 export LD_LIBRARY_PATH=/opt/usr/nx-ks/nx-rc/tools/usr/lib
 $BB nice -n 19 /opt/usr/nx-ks/nx-rc/tools/usr/bin/convert \
   -limit thread 1 -limit memory 32MiB -limit map 0 -limit disk 0 \
   -define jpeg:size=1024x1024 "$SRC[0]" -auto-orient -thumbnail 1024x1024 \
   -quality 82 jpeg:/tmp/nx/thumb/t1024.jpg; echo "convert rc=$?"
 wait'
```

**操作时序**：先按 REC → 确认录起来了 → 再执行上面这条（它会 sleep 10 s 后转码）→ 转码结束再停录。

**判据（A1）**：
- `convert rc=0` 且 `/tmp/nx/thumb/t1024.jpg` 非空（压力确实产生了）
- 采样里 `avail` 最低值记录（这是要测的量，不设阈）
- **成片仍可完整解码**（见 §3.1 判据）。若不可解码 → **立即停，命中 §6 不放行条件**

### A2 候选：录像中提交**真实入口**请求

走 CGI，不是直接调 convert，这样才测得到门控。

```sh
# 确认 web 服务在跑
ssh_cmd 'BB=/opt/usr/nx-ks/busybox; $BB netstat -ltn 2>/dev/null | $BB grep -w 80 || echo "PORT80 DOWN"'
# REC 之后发请求，观察返回是否"降级/排队"
ssh_cmd 'BB=/opt/usr/nx-ks/busybox
 $BB sh /tmp/nx-p0/p0-sample.sh 60 /tmp/nx-p0/A2.tsv >/dev/null 2>&1 &
 $BB renice 0 -p $! >/dev/null 2>&1
 $BB sleep 10
 echo "--- request ---"
 $BB wget -q -O - "http://127.0.0.1/cgi-bin/thumb?f=100PHOTO/SAM_0486.JPG&w=1024&q=82" 2>&1 | $BB head -20
 echo "--- ps ---"
 $BB ps 2>/dev/null | $BB grep -c "[c]onvert"
 wait'
```

**判据（A2，四条全中才通过）**：
1. 请求返回"降级/排队"语义，**不是**"已完成"
2. `ps | grep -c [c]onvert` = 0
3. 采样 `avail` 未跌破 40960
4. 停录收尾确认后，**同一请求重试成功**

### 3.1 成片判据（A0/A1/A2 同一套）

取回 mp4 后在 PC 上验：

```sh
ffprobe -v error -show_entries format=duration,size -show_entries stream=index,codec_type -of json A0.MP4
ffmpeg -v error -i A0.MP4 -f null - 2>&1 | head   # 空跑解码，有错会打出
```

| # | 判据 | 判定 | 不能用什么代替 |
|---|---|---|---|
| 1 | 完整解码无错误 | `ffmpeg -v error -i X -f null -` 无输出 | — |
| 2 | 容器自洽 | `nb_frames ÷ (duration × avg_frame_rate)` 与 1 的偏差 < 0.5%；同时 `size × 8 ÷ duration` 与设定码率相符（±10%） | **不能用「与手动设定的 30 s 比，误差 ≤1 帧」**：手动按 REC 的时刻本身有秒级不确定度 |
| 3 | 音视频轨都存在 | `stream` 里同时有 video / audio | — |
| 4 | MP4 box 结构完整 | 按 box 长度逐层解析：顶层有 `ftyp`/`moov`/`mdat`，**`moov.size` 落在文件长度内**，`mdat.size` 与 `moov` 里的 sample size 之和一致 | **不能用「末 64 字节出现 moov」**：`moov` 通常在文件头，在尾部出现只说明 faststart 未启用，既不证明完整也不证明损坏 |
| 5 | 帧连续性 | 用 `ffprobe -show_frames` 取 PTS，统计相邻差值的异常计数（偏离中位数 >1.5× 的间隔数）；**只与自身比，不与另一条录像比** | **不能用「两次独立录像的抽帧字节差异」**：编码器状态、码率控制、场景内容都不同，字节差无法归因到丢帧或重复段 |

判据 2 与 5 都是**文件内部自洽**，不依赖「录制时长」这个人工不准确的量。判据 4 必须解析 box 长度，不能做字符串搜索。

## 4. P0-B 收尾时间线（5 轮 + 四个边界样本）

每轮：开采样 90 s → 按 REC → 录 10 s → 停录 → 等 30 s 让收尾走完。

```sh
ssh_cmd '/opt/usr/nx-ks/busybox sh /tmp/nx-p0/p0-sample.sh 90 /tmp/nx-p0/B_run1.tsv >/dev/null 2>&1 &
        /opt/usr/nx-ks/busybox renice 0 -p $!; echo started'
```

从 TSV 里抽四个时刻，构成状态机时间线：

```sh
# PC 侧分析：找 wfd 0→1（开录）、1→0（停录）、bytes 停止增长（收尾完成）
awk -F'\t' 'NR>1 && $1 !~ /^#/ {
  if ($4+0>0 && !on) {print "REC   n="$1" sec="$2; on=1}
  if ($4+0==0 && on) {print "STOP  n="$1" sec="$2; on=0}
  if ($5!="NA" && $5+0>last) {last=$5+0; ln=$1}
  print > "/tmp/B1_series.txt"
} END {print "SIZE_STABLE_AT n="ln}' B_run1.tsv
```

### 四个必须覆盖的边界

| 样本 | 怎么造 | 记录什么（不预设结论） |
|---|---|---|
| **B1** | 按 REC 后 **< 1 s** 立刻查 | 同一时刻的 `wfd` / `WFD_GUARD` / `WFD_ANY` / `GUARD_BLIND` / `STATE`。若 `WFD_ANY>0` 而 `WFD_GUARD=0`，即证明守卫对“刚开录”无观察能力 |
| **B2** | 停录后 **fd 刚消失、大小还在涨** 的窗口内查 | 采样里 `wfd=0` 但 `bytes` 继续增长的那几行，与该时刻的 `STATE`。**此处 STATE 会是什么，尚属待测**；在拿到实测前，只能说“原守卫没有表达收尾态的字段” |
| **B3** | `kill -STOP` 相机进程后查，或读一个坏 pid | `p0-state.sh` 的 `RC=1` 与 `STATE=NO_CAMERA/NO_RECORDING_STATE` |
| **B4** | 休眠唤醒 / 拔插卡后立刻查 | 唤醒后第一次查询的耗时与结果 |

### 状态查询的真实入口

**注意：`tc.sh status` 返回的是曲线状态（`CURVE=`/`Y9=`/`DIRTY=`/`GAMMA=`/19 点），不是录像状态。** 它没有录像状态查询动词。

现存唯一的状态判定内联在 `tc.sh::write_guard()`：先 FD 扫描，再 `devmem 0x817dccb8 8` 取 mode。新增只读探针 `scripts/p0-state.sh` 复刻该判定，但**同时输出两套 FD 计数**：

```sh
bash tools/nx-ssh.sh push scripts/p0-state.sh /tmp/nx-p0
ssh_cmd '/opt/usr/nx-ks/busybox sh /tmp/nx-p0/p0-state.sh; echo "exit=$?"'
```

输出字段与含义：

| 字段 | 含义 |
|---|---|
| `STATE` | 按**原守卫**逻辑得出的结论：`NO_CAMERA` / `NO_RECORDING_STATE` / `RECORDING` / `NO_MODE` / `UNSUPPORTED_MODE` / `IDLE`。**`IDLE` 只表示「无 mp4 fd 且 mode=1」，不表示「非收尾」** |
| `WFD_GUARD` | 用原正则 `l-w.*DCIM/.*\.mp4` 数到的 fd 数 |
| `WFD_ANY` | 用修正正则 `l.w.*DCIM/.*\.mp4` 数到的 fd 数 |
| `GUARD_BLIND` | `WFD_GUARD != WFD_ANY` 时为 1，即**守卫没看见** |
| `MODE` / `MP4_BYTES` / `AVAIL` | 原始观测值 |

退出码：0=IDLE、1=NO_CAMERA / NO_RECORDING_STATE / NO_MODE、6=RECORDING / UNSUPPORTED_MODE。

### 关于 `l-w` 这个正则（11:46 实机纠正）

**此前"守卫失明"的结论方向错了，撤回。**

11:46 采集（468 样本，90 秒）实测：

```
rw>0 行: 138
期间 guard>0 行数: 138      ← 全部匹配
```

原因是内核按 `f_mode` 显示 `/proc/<pid>/fd/<n>` 的权限位：

| fd 打开方式 | `ls -l` 显示 | `l-w` 是否匹配 |
|---|---|---|
| 只写 `O_WRONLY` | `l-wx------` | **是**（`l-w` 三段正好对上） |
| 只读 `O_RDONLY` | `lr-x------` | 否 |
| 读写 `O_RDWR` | `lrwx------` | 否 |

**录像 fd 是只写打开**，所以守卫在录像时是有效的。PC 测试原先构造 `lrwx------` 属于错误假设，已改为按 `rw`/`ro` 两种形态生成。

真实漏检面因此收窄为：**只读打开的 mp4 fd**（例如相册预览）显示 `lr-x------`，`l-w` 不匹配。这一条**尚未实测**。

另一个仍然成立的限制：`lrwx`/`l-wx` 只是由 f_mode 派生的权限位，它不能区分"正在写"与"已打开但暂时不写"。判断是否还在增长，仍要看文件大小。

### 通过条件

- 「录制 / 正在收尾 / 未知状态」三态下：零批量转码、零预热、零非必要参数写入（`ps` + 转码日志 + SD 写入计数三重取证）
- 放行只认**收尾确认**，不认「无 fd」
- 状态切换延迟有实测数字（记录，不设上限）
- 任何一态下相册仍返回已缓存缩略图，且不因拒绝而报错

## 5. P0-C 恢复路径注入（5 例）

每例都要**回读确认恢复到了什么状态**，不看退出码。

| # | 注入 | 命令要点 | 期望 |
|---|---|---|---|
| 1 | 删 `lutptr.bak` 后 `off` | `mv` 备份→`lut.sh off`→看退出码 | 明确报 `VIDEOPTR not restored`，rc≠0 |
| 2 | `a18_orig.bin` 置 0 字节后 `off` | 备份原件→`truncate -s 0`→`off` | 走 `$TD/factory/a18_orig.bin` 回退且回读全等 |
| 3 | `begin` 时预置"残留 ready + 指针表已改" | 手工造残留→`lut.sh begin` | 先真还原再备份，备份内容 ≠ 被改表 |
| 4 | `tc.sh` 写入中途 `kill -9` | 写入时杀进程→重跑 `tc_verify` | 能判出非 stock 并给出可恢复路径 |
| 5 | `update_nxrc.sh` 覆盖到一半断电 | `kill -9` 中途→重跑 | 至少能识别"半新半旧"并回到一致版本 |

每例的固定取证模板：

```sh
ssh_cmd 'BB=/opt/usr/nx-ks/busybox
 echo "== before =="; $BB md5sum /mnt/mmc/a18_*.bin /mnt/mmc/lutptr* 2>/dev/null
 echo "== action =="; /opt/usr/nx-ks/lut.sh off; echo "rc=$?"
 echo "== after =="; $BB md5sum /mnt/mmc/a18_*.bin /mnt/mmc/lutptr* 2>/dev/null
 echo "== verify =="; /opt/usr/nx-ks/lut.sh status'
```

**失败即停**：任一例出现「rc=0 但状态不对」的静默成功 → 命中 §6，不再往下测。

**已知必须在开测前修掉的**：`lut.sh:570 begin` 调 `sh $0 off` 后不检查退出码就 `slot read` 备份。第 3 例会直接命中它。建议先改成「非 0 即 die，不备份」，否则第 3 例注定失败且会污染备份。

## 6. 不放行条件（任一命中即阻塞全部后续）

- A1 出现成片不可完整解码，且无法归因到具体资源争抢点
- B1 / B2 采集到的 `STATE` 与 `WFD_ANY` 出现**可复现的矛盾**（守卫说待机、实际有 mp4 fd），且该矛盾直接导致重任务被放行——**需实测到放行行为才成立，仅状态矛盾本身不算**
- §5 任一注入例出现静默成功（rc=0 但状态不对）
- 任何一轮出现 camera daemon 退出

命中后：停止测试 → 导出全部 TSV 与 mp4 → 记录最后一条 `avail` 与 `wfd` → 不当场改代码，先归因。

## 7. 每轮记录模板

```
轮次: A0 / A1 / A2 / B1..B4 / C1..C5
日期:            开机 uptime:             cam pid:
开始前 avail:     最低 avail:             结束 avail:
采样文件:          avg_ms:
操作时序: (按 REC 的时刻 / 停录时刻 / 转码启动时刻，按 n 值记)
结果: 通过 / 失败 / 无效
失败点:
备注:
```

## 8. 执行顺序与预计

| 序 | 项 | 轮数 | 每轮时长 | 说明 |
|---|---|---|---|---|
| 1 | 环境快照 + 采样自检 | 1 | 1 min | 不满足就不往下 |
| 2 | A0 空载基准 | 1 | 60 s | 必须最先，作为比对基准 |
| 3 | B 时间线 | 5 | 90 s | 穿插在 A 之间，趁手 |
| 4 | A1 破坏性 | 1 | 60 s | 单独做，做完等内存回落 |
| 5 | A2 门控 | 1 | 60 s | 依赖 A1 的结论 |
| 6 | C 注入 | 5 | 各 1 min | 建议先修 `lut.sh:570` 再做第 3 例 |
| 7 | apply 分段计时 | 2 档 | — | 「已在 CUSTOM1」/「需切档」分开报 |

先跑 2 和 3：A0 与 B 都是**非破坏性**的，能先把时间线和成片基线拿到；A1 放在它们之后，因为它是唯一可能拔电池的一步。
