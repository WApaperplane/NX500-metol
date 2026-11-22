# 私有设备静态逆查：`/dev/drime5_ep` 与 `/dev/d5_hevc`（2026-11-16）

> 方法：`vmlinux_02.elf` 符号表（37,167 条）+ miscdevice 结构解析 + `file_operations` 解析 +
> ioctl switch 反汇编（capstone 5.0.7）+ 全 .text `bl` xref + 用户态消费者交叉验证。
> **全部为静态结论，未上机。** 证据分级：L1 符号/指令在镜像里 → L2 设备节点在 → L3 实际工作。
> 工具：`tools/nxkernel_syms.py`（符号导出）· `nxkernel_miscdev.py`（设备名）·
> `nxk_ioctl_map.py`（命令表）· `nxk_xref.py`（调用点）· `nxk_callers.py`（分支反汇编）
>
> **修订 2026-11-16（用户只读复核，用 v1.12 内核 + 更完整固件解包）**：
> ① `d5_hevc` 有原厂消费者（`libmmfmodules.so`），"零消费者"作废；
> ② HEVC 命令表 **12** 条不是 13（缺 `0x04`），解码入口是 `0x09`；
> ③ EP 命令 **27** 条（`0x68` 17 + `0x73` 10），初版把 `0x73` 写成 7；
> ④ `nxk_xref.py` 初版只扫无条件 `BL`，已补条件 `BL` 与 `BLX`；重扫后仍是 4 处，
>    但"ioctl 不碰寄存器"的证据强度须按 1.4 收窄。

## 0. 判据基准：三星自己只碰 4 个私有设备

`usr-lib-d5/libudd5.so`（320KB，DRIMe5 用户态驱动库）里的设备串：

```
/dev/d5_ipcc   /dev/d5_lock   /dev/d5_sma   /dev/drime5_ep   (+ /dev/mem ×4)
```

全 `extract/` 目录搜索：`d5_hevc` / `d5_cmdq` / `mali` **在本目录内**零消费者；`drime5_ep` 唯一有消费者。

> **★ 2026-11-16 复核修正（用户提供）**：`d5_hevc` **有原厂消费者**。`libmmfmodules.so`
> （v1.12 与 v1.13 字节一致）中，解码器的 `Initialize()` 与编码器的 `Configure()` 都会
> `open("/dev/d5_hevc")`；固件自带对应的 `dec_m4.bin` / `enc_m4.bin`。
> 本文所有"零消费者"结论**只适用于本次选择性解包**（`di-camera-app` + `st` + `mcbtest` +
> `libudd5.so`），**不适用于完整 rootfs**。实机在哪些拍摄模式下走这条链路仍未验证。
>
> 教训：选择性解包不能用来证明"无人使用"。要证明零消费者，必须扫完整 rootfs 或上机看
> `/proc/*/fd`。

## 1. `/dev/drime5_ep`

### 1.1 基本事实

| 项 | 值 |
|---|---|
| miscdevice name | `drime5_ep`，MISC_DYNAMIC_MINOR |
| `file_operations` @ `c0589228` | `unlocked_ioctl`=`drime5_ep_ioctl`@`c02cd37c`、`mmap`=`d5_ep_mmap`@`c02cd32c`、`open`/`release` |
| 无 read/write/poll | 只靠 ioctl + mmap |
| 寄存器资源 | **10 段**，见 1.3 |
| ioctl | **27 个命令**，两套 type：`0x68`（17 个）、`0x73`（10 个） |

### 1.2 命令表（type 0x68）

| nr | dir | size | 备注 |
|---|---|---|---|
| 0x06 | _IOW | 8 | |
| 0x07 | _IOW | 8 | |
| 0x08 | _IOWR | 12 | |
| 0x09 | _IOW | 12 | |
| 0x0e | _IOW | 4 | |
| 0x10 | _IOW | 4 | |
| 0x11 | _IOW | 4 | |
| 0x12 | _IOW | 4 | |
| 0x13 | _IOW | 4 | |
| 0x15 | _IOW | 4 | |
| 0x16 | _IOWR | 4 | |
| 0x28 | _IOW | 8 | 与 0x06 同构 |
| 0x29 | _IOW | 12 | 与 0x09 同构 |
| 0x32 | _IOW | 4 | |
| **0x3c** | _IOWR | **103** | 寄存器表？ |
| **0x3d** | _IOWR | **117** | 寄存器表？ |
| 0x64 | _IOR | **80** | 读状态块 |

type 0x73 另有：`0x01`(_IOR,4)、`0x02`(_IOWR,4)、`0x03`(_IOWR,4)、`0x04`(_IOR,4)、
`0x05`(_IOWR,8)、`0x06`(_IOW,4)、`0x07`(_IOW,4)、`0x08`(_IOR,4)、`0x09`(_IOWR,8)、`0x0a`(_IOW,4)。

### 1.3 寄存器资源表 `drime5_ep_resource` @ `c06fb1a8`（struct resource = 28 B）

| # | 范围 | 长度 |
|---|---|---|
| 0 | `0x20820000`–`0x20821BFF` | 0x1C00 |
| 1 | `0x20823000`–`0x20823FFF` | 0x1000 |
| 2 | `0x20824000`–`0x20825FFF` | 0x2000 |
| 3 | `0x20826000`–`0x20826FFF` | 0x1000 |
| 4 | `0x20827000`–`0x20827FFF` | 0x1000 |
| 5 | `0x20828000`–`0x20828FFF` | 0x1000 |
| 6 | `0x20829000`–`0x20829FFF` | 0x1000 |
| 7 | `0x2082A000`–`0x2082AFFF` | 0x1000 |
| **8** | **`0x2082B000`–`0x2082BFFF`** | 0x1000 |
| 9 | `0x20821C00`–`0x20821CFF` | 0x100 |

**第 8 项就是 3DLUT 窗口。** EP 的资源表覆盖了整个 `0x20820000`–`0x2082BFFF` 连续区。

### 1.4 关键结论 A：ioctl 不是寄存器写口

`ep_set_top_reg_info` / `ep_dd_set_top_reg` / `ep_set_ldc_reg_info` / `ep_set_mc_reg_info`
的 xref（**已含条件 BL 与 BLX**）结果：**直接调用点只有 4 处**，全部位于 `drime5_ep_probe`
（`c0406240`/`c0406248`/`c0406268`/`c0406288`），各一次。

**证据边界**：这只证明"没有对这四个函数的直接分支调用"，**不等于**"ioctl 不碰寄存器"。
未排除：(a) 通过函数指针的间接调用；(b) ioctl 走的是另外的写寄存器函数（不经这四个）；
(c) 内联展开。结论应限定为：**未发现 EP ioctl 直接调用这四个寄存器表写入函数。**

⇒ 就现有证据，用 EP ioctl 替代 `/dev/mem` 写 LUT 缺少支持；要下定论得逐个 case 跟踪
写内存指令（`str` 到 ioremap 基址），工作量另计。

### 1.5 关键结论 B：但 mmap 是一个不检查范围的物理地址映射口

`d5_ep_mmap` @ `c02cd32c` 全文 20 条指令，核心是：

```
ldr r2, [lr, #0x48]   ; r2 = vma->vm_pgoff
ldr r1, [r1, #4]      ; r1 = vma->vm_start
ldr r3, [lr, #8]      ; vma->vm_end
rsb r3, r1, r3        ; r3 = size
bl  remap_pfn_range
```

`remap_pfn_range(vma, vm_start, vm_pgoff, size, prot)`——**pfn 直接取 `vma->vm_pgoff`，
没有任何范围/白名单校验**。

⇒ 用户态可以：

```c
fd = open("/dev/drime5_ep", O_RDWR);
void *p = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x2082B000);
```

直接把 3DLUT 寄存器窗口映射进来。**理论上可映射任意物理地址**（这不是设计意图，是驱动的疏漏）。

**收益评估（要说实话）**：现有 `/dev/mem` 路径已经跑通，换 EP mmap 没有功能增量，只是
「不用 `/dev/mem`（不需要 CAP_SYS_RAWIO，只依赖设备权限）」。如果 `/dev/drime5_ep` 的
miscdevice mode 是 0666，就能让非 root 进程（如 CGI）做寄存器操作。**值不值得换取决于
mode，上机 `ls -l /dev/drime5_ep` 一查即知。**

## 2. `/dev/d5_hevc`

### 2.1 基本事实

| 项 | 值 |
|---|---|
| miscdevice name | `d5_hevc`，DYNAMIC |
| fops @ `c0589608` | read/write/ioctl/mmap/open/release **齐全** |
| 寄存器资源 `_d5_hevc_resource` | `0x200F0000`–`0x200F0DFF`（0xE00 = 3.5KB） |
| ioctl | **12 个，type 0x74**：nr `0x01–0x0d` 中**缺 `0x04`** |
| 消费者 | **原厂有**（`libmmfmodules.so` 的 dec `Initialize()` / enc `Configure()`），本次解包内无 |

### 2.2 命令表与语义（已定位到函数）

| nr | dir | size | case@ | 调用的底层函数 | 推断 |
|---|---|---|---|---|---|
| 0x01 | _IOR | 4 | `c02d60ac` | — | 读版本/状态 |
| 0x02 | _IOW | 4 | `c02d628c` | — | 单值设置 |
| 0x03 | _IOWR | 8 | `c02d5f5c` | — | |
| 0x05 | _IOWR | 8 | `c02d5ba8` | — | |
| 0x06 | _IOWR | 8 | `c02d5f20` | — | |
| 0x07 | _IOWR | 8 | `c02d637c` | — | |
| **0x08** | _IOWR | **268** | `c02d5e6c` | **`host_model_main`**@`c02d5490` | **编码入口** |
| **0x09** | _IOWR | 176 | `c02d5d2c` | **`host_decoder_main`**@`c02d4e20` | **解码入口** |
| **0x0a** | _IOWR | **268** | `c02d5dec` | **`get_encoded_data`**@`c02d51e8` | **取编码结果** |
| 0x0b | _IOWR | 176 | `c02d6328` | — | 收帧 |
| 0x0c | _IOWR | 4 | `c02d5ca0` | — | |
| 0x0d | _IOWR | 4 | `c02d5fa8` | — | |

命令总数为 **12**（`nr` `0x01`–`0x0d`，分派里**没有 `0x04`**）；初版误写 13，已按复核修正。

编码与解码各占一组:

- 编码：`0x08`（268 B → `host_model_main`）+ `0x0a`（268 B → `get_encoded_data`）
- 解码：`0x09`（176 B → `host_decoder_main`）；`0x0b`（176 B）与其同尺寸，语义未定

### 2.3 268 B 结构已见字段

`d5_hevc_ioctl` 栈帧 `sub sp, #0x130`（304 B），结构副本落在 `sp+0`..`sp+0x10C`。
case 0x08 的字段检查序列：

```
ldr r7, [sp, #0xac]     ; r7 = 结构内某个指针字段（+0xAC）
ldr r0, [r7, #8]        ; 检查 +8
ldr r0, [r7, #0xc]      ; 检查 +0xC
ldr r0, [r7, #4]        ; 检查 +4
ldr r0, [r7]            ; 检查 +0
...
mov r0, sp
bl  host_model_main     ; 整个 268B 结构作为参数
```

⇒ 结构 `+0xAC` 处是**子结构指针**，子结构 `+0x00/+0x04/+0x08/+0x0C` 是四个指针（多半是
输入/输出/重建/参考帧 buffer），驱动在调用前逐个判空。

**未完成**：字段全表（约 20–30 个）需要逐条跟踪 `[sp, #N]` 才能确定；`host_model_main`
内部还要看它怎么用这些指针。

### 2.4 判断

- 有硬件寄存器区（3.5KB）⇒ **不是纯软件编解码**
- 但入口函数叫 `host_model_main`（H.264/H.265 参考软件命名习惯）⇒ 大概率是**寄存器命令序列
  生成 + 码率控制**，实际像素运算仍由硬件做
- 编码（`0x08`）与解码（`0x09`）各自定位到独立入口，不是同一函数的参数分支
- **原厂有消费者**（`libmmfmodules.so` 的 enc/dec 模块 + `enc_m4.bin` / `dec_m4.bin`），
  但实机在哪些拍摄模式下启用未验证 —— **不能再称"这条通路是死的"**

## 3. 结论与建议

### 3.1 已排除

| 设想 | 结论 |
|---|---|
| 用 EP ioctl 替代 `/dev/mem` 写 LUT 槽/寄存器表 | **缺证据**。未发现 ioctl 直接调用那四个寄存器表写入函数；但间接调用/其它写寄存器函数未排除（见 1.4） |
| `d5_cmdq` 作为 ISP 命令提交口 | 只有 2 个命令（`type=0x74 nr=0x04/0x05`，size 24），且**零消费者**。不足以支撑寄存器写 |
| CMA（`d5_cma_*`）给用户态分配连续内存 | **不成立**。只有 4 个内核 API、无 cdev。合法零拷贝走 DRM GEM + dma-buf |

### 3.2 已打开

| 项 | 结论 |
|---|---|
| EP mmap | 不检查 pgoff，可直接映射 `0x2082B000`；是否值得替换 `/dev/mem` 取决于设备 mode |
| `d5_hevc` | 完整 ioctl API 已解出（**12** 命令 + 编码/解码入口 + 结构首字段）；**原厂 `libmmfmodules.so` 在用它**，不是死代码 |
| `host_decoder_main` | 存在，可用于视频缩略图——但前置是 MP4 demux + 268B 结构全解 |

### 3.3 下一步的性价比排序（已按"d5_hevc 有原厂消费者"重排）

1. **先看 `libmmfmodules.so` 的导出符号**——原厂解码器已封装 `Initialize()` / `Configure()` 与
   `/dev/d5_hevc` 的全部细节。若能直接链接（或照它的调用序列仿写），视频封面就不必逆 ioctl，
   成本降一个数量级。**当前最便宜的一条。**（已完成，见第 4 节）
2. 同时查 MP4 里有没有现成封面（`covr` / 自定义 atom）——若相册只要一张静态封面，连解码都不用做。
3. 上机 `ls -l /dev/drime5_ep` 看 mode → 决定 EP mmap 有没有实际意义。
4. 逆 268 B 结构全表降为最后手段（中高工作量）。

## 4. `libmmfmodules.so`：原厂媒体框架 HAL（2026-11-16 补，已从 rootfs 提取）

### 4.1 提取路径

`NX500_v1.12.bin` 是 SLP 容器，10 段；第 7 段（offset `27755905`，len `280505658`）是 LZOP 流。
`lzop -d` 后得到 **534,056,960 B 的 ext4，volume name = `rootfs`**。

```sh
# 提取段 7（python 分块，避免整包进内存）
# WSL: lzop -d -f -k p6.lzo -o p6.img
# WSL: debugfs -R 'dump /usr/lib/libmmfmodules.so ./libmmfmodules.so' p6.img
```

产物：`/usr/lib/libmmfmodules.so`（315,716 B，ARM ELF，inode 11988，0755）
固件同时自带 **`/usr/share/hevc/dec_m4.bin`（61,804 B）** 与 **`enc_m4.bin`（52,452 B）**。

### 4.2 导出面（722 个函数符号，C++ 名称修饰）

| 类 | 关键方法 | 用途 |
|---|---|---|
| `cHalHevcVideoDecoder` | `Initialize(eMmfMode)` / `Configure(tVideoCodecConfig*,tVideoExtraData*)` / `Decode(tVideoStrm*,tMmfYuvData*)` / `initDecodedInfo(d5_hevc_dec_decoded_info*)` | **HEVC 解码** |
| `cHalHevcVideoEncoder` | `Initialize` / `Configure` / `Encode(tMmfYuvData*,tVideoStrm*)` / `SetHevcEncMode` | HEVC 编码 |
| `cHalMJpegVideoEncoder/Decoder` | `Encode` / `Decode` / `QTControl` | MJPEG |
| `cHalJpeg`（单例 `pInstance`） | `Encode(tMmfYuvData*,tMmfData*)` / `Decode` / `Configure(int,int)` / `SetMaxSize` / `controlQT` | **JPEG 编解码** |
| `cMmfCamera` | `Initialize(...)` / `SetFrameRate` / `FrameupdateAndCalcAdjust` | 相机帧回调 |
| `cMP4TrimParser` | `GetFrameInfo` / `GetExifFrameInfo` / **`mp4_get_video_key_frame`** | **MP4 解析、找关键帧** |
| `cMP4TrimWriter` | `mp4_write_video_frame` / `mp4_writer_open` | MP4 写出 |
| C 便捷接口 | **`mmf_jpeg_encode(tMmfYuvData*,tMmfData*)`** / `mmf_jpeg_decode` / `mmf_jpeg_configure` | 无修饰，最易调用 |

### 4.3 `cHalHevcVideoDecoder::Initialize` 的实际序列（0x269ec，反汇编）

```
setBufferSize()
open64("/dev/d5_hevc")                    ← 设备打开点
initDecodedInfo(d5_hevc_dec_decoded_info*)  ×2
ipccDecInit(uint)                          ← ★ 走 IPCC（A9↔p7 通道）
cMmfMutex::Lock()
ioctl(...)                                 ← 命令下发
cMmfMutex::Unlock()
RequestImageBufferMemoryEx(...) ×2
tint::request_memory(uint, Cache) ×2
```

**★ 关键约束：解码链路依赖 IPCC。** 仓库已有记录 `mipi_csis_dma` 两次挂机在 IPCC/CSIS 私有设备上，
所以**自己照着 ioctl 重写解码器是在踩雷区**；正路是链接 `libmmfmodules.so`，让原厂封装管 IPCC 时序。

### 4.4 视频封面的可行路径（按代价排序）

| 路径 | 代价 | 风险 |
|---|---|---|
| MP4 内嵌封面（`covr`/自定义 atom） | 查一次文件头 | 无 |
| 链接 `libmmfmodules.so`：`cMP4TrimParser` 定位首帧 + `cHalHevcVideoDecoder::Decode` + `mmf_jpeg_encode` | 逆 3 个结构体（`tVideoCodecConfig` / `tVideoStrm` / `tMmfYuvData`）+ C++ ABI 匹配 | IPCC（原厂封装内部处理） |
| 自己按 ioctl 重写 | 逆 268 B 全表 + IPCC 协议 | **高**（挂机史） |

### 4.5 待确认

- `tVideoCodecConfig` / `tVideoStrm` / `tMmfYuvData` 三个结构体的字段布局（无 DWARF，`/usr/lib/debug` 为空）
- `d5_hevc_dec_decoded_info` 的内核侧定义（可从 `vmlinux` 的 `initDecodedInfo` 调用点反推）
- 实机哪些模式在用这条链路（需 `ls -l /proc/*/fd | grep d5_hevc`）

## 5. rootfs 完整拆解（451 MB，13,752 文件）

### 5.1 布局

| 目录 | 体积 | 内容 |
|---|---|---|
| `usr/lib` | 180M | 共享库（含 MMF 媒体栈） |
| `usr/share` | 108M | 字体、声音、edje 主题 |
| `usr/apps` | 97M | 8 个 Tizen 应用 |
| `usr/bin` | 35M | 命令 |
| `lib` | 9.3M | 基础库 |
| `initrd` | 5.7M | 初始 ramdisk |
| `etc` | 4.4M | 配置、systemd |

`/opt` **为空** —— NX-KS 装在可写分区 `/opt/usr/nx-ks/`，不在 rootfs 里，所以恢复出厂不会丢 mod（前提是别刷 p10）。

应用清单：`di-camera-app`、**`di-thumbnail`**、`smart-wifi-app`、`ap-setting-app`、`app-selector`、`help`、`org.tizen.browser`、`org.tizen.policy`、`bluetooth-testmode`。

### 5.2 ★ 两套「官方图像/视频库」——比自研工具链更值得复用

#### (a) `libmm-photo.so`（999,892 B，1893 个导出）—— 完整图像处理

| 用途 | API |
|---|---|
| 解码到 YCC | `mm_photo_read_image(path, eDecodeImgType, CYcc&, bool)` / `mm_photo_decode_image` / `mm_photo_decode_jpeg` |
| 编码 | `mm_photo_encode_to_file(path, sImgSize)` / `mm_photo_encode_to_buffer` / `mm_photo_extract_jpeg_to_file` |
| **缩放** | **`mm_photo_resize_ycc(src, dst)`** |
| 旋转 / 色彩 | `mm_photo_rotate_ycc` / `mm_photo_convert_to_rgb` / `mm_photo_rgb_to_ycc` |
| 直方图 | `mm_photo_make_ycc_distribution_array` / `..._rgb_...` |
| 质量 | `mm_photo_set_jpg_quality(tImageQuality)` / `mm_photo_get_jpeg_max_size(w,h)` |
| **RAW** | **`CaptureDataParser::convertRaw(stCAPTURE_IMAGE&, sImgComposition&)`**、`mm_photo_read_image_withSWDD` |
| 编辑器 | `CPhotoEditor`（Init/undo/redo/save/applyPreviewEffect）、`ImpEffector::executePhotoEditor` |
| 设备侧 | `CMmDeviceDriver::DecodeJpeg` / **`MakeScreenThumb`** / `ConvertYccSetToJpegSet` |

**依赖链（关键）**：`libsif.so`、`libredeye.so`、**`libudd5.so`**、`libface.so`、
`libdi-alg-ipplatform.so`、`libcapture-fw-prod.so`、**`libmulticore-bridge.so`**、`libstdc++.so.6`。

⇒ **它的缩放/编码不是纯软件，是经 `libudd5`（EP/SMA）+ 多核桥走硬件的。**
这正好是「1024 px 冷转码 6.39 s、内核无 GSC/FIMG2D」那个死结的官方解法。

#### (b) `libmmf_player.so`（80,160 B，125 个导出）—— 视频播放与抓帧

| 用途 | API |
|---|---|
| **抓帧（视频封面）** | **`mmf_player_do_video_capture`** (+ `_mmfplayer_do_video_capture_mmf`) |
| 剪辑 | `mmf_player_start_trimming` / `stop_trimming` / `create_editor` / `realize_editor` |
| 播放控制 | `create` / `realize` / `start` / `pause` / `stop` / `set_position` / `get_position` / `frame_advance` / `frame_reverse` |
| 元信息 | `get_video_frame_num` / `get_aframe_info` / `get_state` / `set_message_callback` |

只依赖 `libc` —— 薄封装，真正解码在 MMF 内部。

### 5.3 消费者实证

- `di-camera-app` → `libmmfcore` → `libmmfmodules` → `/dev/d5_hevc` + IPCC
- **`di-thumbnail`** 同时导入 `mm_photo_*`（静态图）与 **`mmf_player_*`（含 `do_video_capture`）** ——
  相机自己的缩略图/播放就是靠这两套
- `smart-wifi-app` 也链 `libmmfcore`

### 5.4 对现有项目的直接意义

| 现有痛点 | 官方解法 |
|---|---|
| 1024 px 冷转码 6.39 s（ImageMagick 软件缩） | `mm_photo_resize_ycc` + `mm_photo_encode_to_buffer` |
| 相册无视频封面 | `mmf_player_do_video_capture` |
| RAW 无预览 | `mm_photo_read_image`（eDecodeImgType 含 RAW）+ `CaptureDataParser::convertRaw` |
| 缩略图靠 `nxthumb.arm` 直切（只能拿机内衍生图） | `CMmDeviceDriver::MakeScreenThumb` |

### 5.5 风险与未验证

1. **链接代价**：`libmm-photo.so` 会拉进 `libudd5`（打开 `/dev/drime5_ep`/`/dev/d5_sma`/`/dev/d5_ipcc`）
   与 `libmulticore-bridge`。独立 CGI 进程加载它，可能**初始化整个媒体栈并与相机 app 抢设备**。
   安全做法是模仿 `di-thumbnail` 的进程模型，而不是塞进 busybox CGI。
2. `eDecodeImgType` 的枚举值未知（要逆 `PictureDecoder::decodeImage` 的 switch）。
3. `mm_photo_resize_ycc` 是否真走硬件、速度多少，**未实测**。
4. `do_video_capture` 的回调协议（`set_message_callback` 的 message 类型）未逆。

## 6. `mm_photo_resize_ycc` 的 ABI（2026-11-16 逆出）

### 6.1 结构体（全部经 getter / operator== 交叉验证）

```c
struct sImgSize { int w; int h; };            /* 8 B  — 构造函数 store +0/+4 */
struct sYccAddr { uint32_t y, c; };           /* 8 B  — 同上 */

struct CYcc {                                 /* 24 B，6 个 32-bit 字段 */
    int      width;    /* +0x00  GetWidth() 读 [this]        */
    int      height;   /* +0x04  GetHeight() 读 [this+4]     */
    int      stride;   /* +0x08  GetStride() 读 [this+8]     */
    uint32_t y_addr;   /* +0x0C  GetY0() 直接返回它          */
    uint32_t c_addr;   /* +0x10  GetAddr() 拷 [+0xC,+0x10]   */
    int      type;     /* +0x14  GetType()                   */
};
```

**验证方法**：`CYcc::operator==`（`0x31c0`）逐字段比较，命中 `+4/+8/+0xC/+0x10/+0x14`
（`+0` 为无位移访存）；`GetY0`（`0x2c80`）`ldr r3,[this,#0xc]; mov r0,r3`；
`GetAddr`（`0x2b9c`）先断言 `+0xC`、`+0x10` 非零，再 `ldm`/`stm` 拷 8 字节。

### 6.2 构造函数

| 重载 | 地址 | 说明 |
|---|---|---|
| `CYcc()` | `0x1698` | 默认构造，字段清零 |
| `CYcc(const sImgSize&, const sYccAddr&)` | `0x16d4` | 用已有内存构造 |
| `CYcc(const sImgSize&, unsigned long memMode, ImageYCCType)` | `0x1f6c` | **自己分配内存**（memMode 选内存池） |
| `CYcc(const sImgSize&, unsigned long, int, ImageYCCType)` | `0x2438` | 多一个 int（可能对齐/填充） |

### 6.3 ★ `mm_photo_resize_ycc` 走硬件，不是软件算法

`0x382e4`（3952 B）的调用统计：

```
CMmDeviceDriver::ResizeImage(CYcc&, CYcc&)   ×2   ← 主路径
CMmDeviceDriver::BlockCopy(CYcc&, CYcc&)     ×2   ← 尺寸相同时直接拷
CMmDeviceDriver::GetInstance()               ×4
CMemoryPool::GetAddr(eMemMode)               ×1
CYcc::CYcc(sImgSize&, m, ImageYCCType)       ×1   ← 内部构造临时目标
GetWidth/GetHeight/GetType/GetY0/GetC0/GetStride  大量
```

⇒ 逻辑是：比尺寸 → 相同走 `BlockCopy`，不同走 **`ResizeImage`（EP 的 SRSZ 缩放器）**。
与 `libudd5.so` 里的 `d5_ep_srsz_*` 导出一致。**这就是 6.39 s 的解法所在。**

`mm_photo_init`（`0xd890c`）只调 `__dlog_print`/`memset`/`strrchr`/`strstr`，
**不打开任何设备** —— 硬件在首次 `CMmDeviceDriver::GetInstance()` 时才拉起。

### 6.4 调用骨架（待补枚举值）

```c
mm_photo_init();                       /* 纯软件初始化 */

CYcc src;                              /* 默认构造 */
mm_photo_read_image(path, eDecodeImgType_?, src, false);   /* 解码进 src */

CYcc dst( sImgSize(W, H), memMode_?, ImageYCCType_? );    /* 自己分配目标 */

mm_photo_resize_ycc(src, dst);          /* → ResizeImage（硬件） */
mm_photo_encode_to_file(dst, sImgSize(W,H));   /* 落盘 */
mm_photo_end();
```

### 6.5 还差的四个枚举/取值（都可用试错定，不必全逆）

| 未知 | 影响 | 定法 |
|---|---|---|
| `ImageYCCType` | `type` 字段 | 遍历 0..7 试构造，看哪个成功 |
| `memMode`（unsigned long） | 目标内存池 | 同上，0 或 1 最可能 |
| `eDecodeImgType` | 读图格式 | 逆 `PictureDecoder::decodeImage` 的 switch |
| `mm_photo_encode_to_file` 第 2 参 | 输出尺寸 | 反汇编该函数看它读 `sImgSize` 哪些字段 |

### 6.6 风险

首次 `CMmDeviceDriver::GetInstance()` 会经 `libudd5` 打开 `/dev/drime5_ep`。
**测这个必须相机待机、且不做其它操作**；出现异常立即停，不要重复试。
