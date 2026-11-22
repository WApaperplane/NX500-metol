# NX500 GPU 对照基准

在 WSL/Linux 构建：

```sh
bash test_server/gpu/build_bench.sh
```

新产物是 `test_server/gpu/out/gpu_bench.arm`。目录顶层旧 `gpu_bench.arm` 是 v2 历史产物，未覆盖；新源码需同时保留 `bench_reference.h`。

把新产物放到相机 `/tmp`，确认没有录像后运行：

```sh
DISPLAY=:0 LD_PRELOAD=/usr/lib/libnative-buffer.so \
  /opt/usr/nx-ks/busybox timeout -t 60 -s KILL ./gpu_bench.arm 20
DISPLAY=:0 LD_PRELOAD=/usr/lib/libnative-buffer.so \
  /opt/usr/nx-ks/busybox timeout -t 60 -s KILL ./gpu_bench.arm 20 resident
```

默认 `roundtrip` 每次上传256²源图、绘制、回读128²/256²结果；`resident` 在采样前上传源图，计时段只绘制并 `glFinish`，其后的正确性读回不计入。两种模式都不是窗口显示帧率测试。无需停止原厂服务。

每种尺寸/操作先运行2次预热，再采集1–100次（默认20次）。copy、矩阵和独立RGB 1D LUT都逐像素对照整数CPU参考；允许每通道误差3/255。该参考只适用于本基准固定矩阵、固定1D LUT及1x/2x缩放，并非优化后的通用CPU图像库。CPU基线不含JPEG解码。1D LUT不是Metol的17³ YCbCr 3D LUT。

`BENCH` 前三列仍是 upload/draw/read 的各段最小微秒数，不能相加冒充完整样本；resident未执行的上传/回读段输出0。`total_min_us`、`total_p50_us`、`total_p95_us` 是完整计时段统计，`cpu_p50_us`/`cpu_p95_us` 是CPU参考的墙钟时间，`gpu_cpu_p50_us` 是GPU计时段的调用进程CPU时间。CPU参考校验、输出和resident校验读回不在GPU计时段。进程CPU时间不含X11服务或其他内核线程。计时精度以设备时钟为准；本机结果可见约400μs量化。

GL错误、像素不符、采样不完整均非零退出；非法参数也非零退出。`nz`仅为历史诊断字段，不是正确性判据。单测覆盖已知像素、缩放、饱和、清屏/错误shader输出拒绝及分位数；真实EGL路径仍须在相机验证。

实测、局限和投入优先级见 [2026-11-22 评估](../../docs/current/OPERABILITY_GPU_REVIEW_2026-11-22.md)。
