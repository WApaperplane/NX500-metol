# Metol 运行架构与功能演进设计

基线为本地 NX500 1.12 工作区。第一阶段保留 NX-KS 的启动入口和现有硬件后端，优先让录像、相册和更新共享明确的运行状态。NX1 1.41 单独声明已测能力，默认沿用现有降级路径。

## 现状与实测边界

- NX500 是单核 ARMv7，搭载 Linux 3.5、Tizen 2.2.0 和 systemd 208。现有证据记录了休眠恢复时 PID 1 的状态会保留，unit 文件的更新不会自动被重新读入。重启官方相机服务曾导致卡死。模组用户态服务可以沿用已验证的启动入口做幂等检查。 [设备架构](NX500_ARCHITECTURE.md) · [休眠与投递](HANDOVER_2026-10-16.md) · [服务实机记录](CAMERA_ONLINE_PROBE_2026-10-13.md)
- 相册已有 JPEG 内嵌图直切、按视口请求、带锁的转码和 40 MiB 准入线。11 月 16 日录制并发中，20 张网格图的 p50/p95 为 150/220 ms，单张 1024 px 冷转码耗时 6.39 秒，期间采到的最低 `MemFree + Cached` 为 55.3 MiB。两段测量前后可写 MP4 路径一致，该轮最终成片的播放完整性仍待验证。 [基准脚本](../../test_server/bench-record-concurrency.py) · [缩略图路径](../../scripts/nx-rc/thumb/thumb-cgi) · [转码准入](../../scripts/nx-rc/thumb/thumb-cache.sh)
- LUT 已具备首帧装载和单独的录像 watcher。NXUI 已有事件循环和选项合并。FilmLab 已有自己的写入锁和档位校验。后续收益主要来自跨入口调度、减少重复工作和验证端到端结果。 [LUT 录像记录](../../docs/releases/NX-WA-2.88.0-nxwa.15.4.md) · [NXUI 事件循环](../../test_server/nxui/nxui.c) · [FilmLab](../../scripts/filmlab.sh)
- 本轮 `.108` SSH 超时。厂商内核启用的事件接口、I/O 调度器和 cgroup 控制器须以实机探针为准。

## 1. 录像优先的后台任务管理

第一版由现有入口共享状态读取和准入判断。复用已验证的可写 MP4 探测，统一识别待机、录制、正在收尾和未知状态。探测时同时校验相机进程身份和记录的时效。读取失败或唤醒后状态过期时，等真实状态恢复再接受重任务。LUT watcher 的时序先保持现有实现，单独测定管理任务带来的收益。

| 工作 | 待机 | 录制、收尾或状态未知 |
|---|---|---|
| 看图 | 优先直切、缓存命中，其余有界转码 | 默认允许直切和命中缓存。冷图先降到嵌入小图或返回可重试状态 |
| 批量后台工作 | 合并同一请求，单路执行。先用户当前视口，再限量预热 | 暂缓批量转码、预热和后台同步 |
| 相机参数写入 | 按资源排队，执行现有前置条件、读回和恢复 | 仅允许有录像兼容性证据的操作。TC 沿用待机限制 |
| 更新与清理 | 允许显式触发，先校验完整包 | 延后到文件收尾并确认待机 |

先保留单转码与 40 MiB 准入线，同时采集 `MemFree`、`Cached`、`Dirty`、`Writeback` 和已有的 `Shmem`、`SReclaimable`。把 `MemFree + Cached`、进程峰值 RSS、耗时和失败原因一起看。Linux 3.14 的 `MemAvailable` 估算会扣除内存低水位并折减可回收缓存。当前求和口径用于同机对照，40 MiB 是已有策略的初始值。先比较软件限速和减少工作量。有证据表明 I/O 调度器可区分进程优先级时，再测试对后台任务降低 I/O 优先级。 [Linux 内存报告实现](https://github.com/torvalds/linux/blob/v3.14/fs/proc/meminfo.c) · [相册转码实现](../../scripts/nx-rc/thumb/thumb-cache.sh)

若现场数据表明多个短进程每次读状态或扫目录产生可见负担，再引入随 Web/作业启动的轻量服务。通过仅供本机进程使用的 Unix socket 返回状态和任务 ID，进程首次启动或恢复后自行重建状态。任务队列有上限，连续预览仅保留最后一个待执行请求。写入任务沿用现有控制器并按共享资源确定锁序，完成前以硬件回读为准。长期常驻和更强隔离需要测过空闲 RSS、唤醒次数和真实固件能力再决定。故障处理仅针对模组进程，并可通过现有入口切回原路径。 [NXUI 执行与去抖](../../test_server/nxui/nxui.c) · [systemd 208 服务语义](https://github.com/systemd/systemd/blob/v208/man/systemd.service.xml) · [Tizen 系统启动](https://github.com/Samsung/tizen-docs/blob/master/docs/platform/porting/kernel.md)

## 2. 文件完成事件、实时相册和导入

先在目标机验证 DCIM 实际写入过程。上游 Linux 3.5 提供 `IN_CLOSE_WRITE`、`IN_MOVED_TO` 和 `IN_Q_OVERFLOW`。以 inotify 监听当前卡上的 DCIM 根目录和正在使用的子目录，新增子目录时添加监听并补扫。拔插卡、溢出或唤醒后重新识别挂载并重建索引。无法监听的旧机型可以用低频目录扫描继续服务。 [Linux 3.5 inotify 标志](https://github.com/torvalds/linux/blob/v3.5/include/linux/inotify.h) · [现有目录接口](../../scripts/nx-rc/thumb/dirlist-cgi)

文件先进入待确认集合。去重以当前挂载代次、路径和文件版本为依据。照片经尺寸稳定和格式头检查后可发布。录像以关闭或重命名事件触发检查，确认目标没有写入者且容器可读，再标为可导入。异常停录保留错误状态。浏览器沿用现有端口和静态资源，增量读取短响应。队列过旧或卡代次改变时请求完整列表。现有全量列表保留作兼容入口。借鉴 OpenIPC 的片段关闭语义和 gPhoto2 的文件新增回调，两者在 Metol 上由当前相机的实测行为重新定义。 [当前前端目录轮询](../../scripts/nx-rc/web_root/js/gallery.js) · [OpenIPC 录制完成事件](https://github.com/OpenIPC/wiki/blob/master/en/motion-events.md) · [gPhoto2 事件处理](https://github.com/gphoto/gphoto2/blob/master/gphoto2/main.c)

这个事件源可以直接支撑四个功能。

1. **联机导入与断点续传**。相机返回已完成的交付列表，PC 拉取临时文件，核对大小和校验后转正。以相机 ID、卡代次、路径和内容版本去重，断线后恢复。需要断点续传时验证现有传输是否支持偏移读取，再选用最小的扩展。 [rsync 的保留部分文件设计](https://github.com/RsyncProject/rsync/blob/master/rsync.1.md)
2. **RAW+JPEG 配对与拍摄侧车**。对同一次捕获配对 JPEG、SRW、缩略图和可确认的参数。由模组发起且有动作 ID 的捕获可以写入 FilmLab/LUT/TC 标识，手动拍摄的未知字段留空。PC 端索引和解码 SRW，可在 PC 生成预览、视频封面和接触印样。 [LibRaw NX500 支持表](https://github.com/LibRaw/LibRaw/blob/master/src/tables/cameralist.cpp)
3. **可恢复的拍摄作业**。复用社区对焦堆栈与延时工具，加入作业预检、进度、暂停和已确认成片计数。以完成事件确认上一步并推进下一步。暂停在两张之间生效，发生写入不确定性时避免重复拍摄。手动 REC 继续作为已验证入口。多机协作可从同时准备、记录各机时差、独立确认交付开始。 [NX500/NX1 上游](https://github.com/ottokiksmaler/nx500_nx1_modding) · [NX500Capture](https://github.com/ajavamind/NX500Capture)
4. **带预检的拍摄预设**。把 FilmLab、LUT、TC 和已验证的拍摄参数引用存为场景。应用前按机型、拍摄模式和当前状态检查组合，逐步写入并读回，在可恢复边界失败即回到上一状态。把通过验证的组合登记到能力表，逐组覆盖 3DLUT 输入域、取景与成片差异。同一场景可以作为拍摄作业和侧车记录的共同标识。 [现有预设协议](T2_DELIVERY_AND_PRESET_2026-10-16.md) · [TC 和 LUT 约束](../../README.md)

## 3. 缩略图和存储写入

常见 JPEG 继续走现有直切。若日志显示冷转码仍经常挡住用户请求，先做 `libjpeg-turbo` 单路原型。用缩放 IDCT 与逐行解码限制中间拷贝，单独验证竖图方向、渐进 JPEG 和 1024 px 画质，非 JPEG 由现有回退处理。发布候选先走现有 ARMv7/glibc 2.13 构建与机身加载验证。缓存键包含原图版本、方向、尺度和生成器版本。设置容量和清理节奏，仅延后清理可重建的缓存。 [libjpeg-turbo 缩放解码源码](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/djpeg.c) · [现有码路](../../scripts/nx-rc/thumb/thumb-cache.sh)

状态按生命周期落盘。RAM 中保存短期任务和诊断环，卡上保存素材和用户导入导出，机身 ext4 可存放高频状态与模组版本。光学与 ISP 的会话状态在唤醒后重新读回。保留 `nxstore` 的上一版和候选文件，先把频繁小写入批量化，再验证将全局 `sync` 收口到目标文件、父目录或文件系统。机上 exFAT 覆盖式重命名已有异常证据，其恢复路径要保留并用拔插和中断对照验证。 [存储事务](../../scripts/nxstore.sh) · [exFAT 实测](LUT_CRCBY_ONDEVICE_FIX_2026-10-25.md) · [Linux 3.5 同步实现](https://github.com/torvalds/linux/blob/v3.5/fs/sync.c)

## 4. 增量更新与恢复

Wi-Fi push、SD 同步和 SSH 部署先共用清单格式，声明版本、机型、固件、能力、入口及每个文件的摘要。从 Web 相册扩展试点，在机身 ext4 创建独立版本目录，完整校验并同步后，在同一目录通过重命名切换启用指针，并同步父目录。运行进程绑定实际版本路径。更新器检查列表接口、缩略图和构建 ID，探测通过后标记成功，否则用上一版恢复同一个模组入口。安装状态与构建 ID 可在机身和 SD 两侧核对。写命令经现有受信 SSH 或经明确认证的入口。只读状态可以沿用 Web，操作清单采用固定参数和动作。这里借鉴 RAUC 的就绪确认与旧版本保留，在模组文件层实现。 [当前增量更新](../../scripts/update_nxrc.sh) · [打包清单](../../tools/pack_release.py) · [Wi-Fi push 文档](../../scripts/nx-rc/push/README.md) · [RAUC 启动确认](https://github.com/rauc/rauc/blob/master/docs/basic.rst)

## 实施顺序与验收

| 阶段 | 实施与验证 | 验收建议 |
|---|---|---|
| 第一批 | 统一录像状态与相册准入，补齐诊断环和成片验证。用当前并发脚本增加对最终文件的 PC 读回。保留更新事务的原型为独立提交 | 同卡同码率做空载与两种相册负载对照。20 张直切的 p95 以 220 ms 为现有基线，候选目标不超过 250 ms。录制、停录、文件落盘均有证据，PC 检查头尾可读、时长与预期相符，并对照丢帧和音画连续性。40 MiB 以下的冷请求可明确降级并在释放资源后恢复 |
| 第二批 | 实机验证 inotify 后接相册增量，先内测版本化更新与回滚 | 模拟事件溢出、同名替换、掉线、卡拔插与多次休眠唤醒。事件恢复后列表与全扫一致。更新中断仅启用完整旧版或新版，重复执行可恢复。记录空闲 RSS 和 CPU、网络请求和 SD 写入频率 |
| 第三批 | 上线 PC 联机导入与 RAW+JPEG 配对，再接延时和堆栈作业。根据实际回退占比决定是否替换 JPEG 解码器 | 对照漏片、重复导入、无效成片和中途再连。新解码器同机同素材报告 p50/p95、峰值 RSS、方向与画质，收益达到要求后再放到默认路径 |

每批都有相同的冷、热相册样本和同卡同码率的录像基线。采样报告携带固件版本、端点实际版本和任务原因。先在 NX500 验收，能力表可分别记录 NX1 和其它固件的支持情况。
