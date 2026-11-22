#!/usr/bin/env python3
"""bake_log.py —— 把 log 曲线烘进 3D LUT（LUT 与"V-Log"同时启用的唯一可靠办法）

背景（2026-11-20 实测）：
  · RC5 的 gamma 引擎（native 事务）在这台机器上能把相机搞重启，弃用；
  · TC 曲线的 19 节点写入时好时坏（`tc.sh set log` 会 ERR=REFRESH_FAIL 回滚）；
  · 唯一稳定的是「写 A18 3D LUT」这条（lut.sh apply，录像/取景/照片同路径）。
  ⇒ 想要 log 的平缓曲线 **和** 自己的风格同时生效，就把两者烘进同一张表。

数据格式（机端 .bin，19712 B）：
  idx = ((Y*17 + Cb)*17 + Cr) * 4     输入是 YCbCr 网格
  每项 4 字节：{Cr, Cb, Y, pad}        输出是映射后的 Cr/Cb/Y
  中性 = 128

本工具只改输出的 Y（用 tc/log.tcy 那条 19 点曲线重映射），可选按局部增益
同比例缩放 Cb/Cr（避免压缩暗部后颜色更艳）。不动输入网格、不动表长。

用法：
  python bake_log.py looks/fuji_classicchrome.bin -o vk_fuji.bin
  python bake_log.py looks/*.bin -d out_vlog/            # 批量
  python bake_log.py x.bin -o y.bin --curve scripts/tc/flat.tcy --chroma 0.6
"""
import argparse
import pathlib
import sys

SIZE = 19712
N = 17


def log_curve(k):
    """解析 log 曲线：Y' = log(1 + k·y) / log(1 + k)，k 越大越平。

    ★ 不要直接拿 scripts/tc/*.tcy 来烘：那 19 点是 TC 的"显示→场景"反 gamma 表
      （flat 在输入 64 处给 21），直接套到 LUT 输出上得到的是**加对比**，
      不是 log。log 要的是抬暗部、压高光，方向正好相反。
    """
    import math
    denom = math.log(1.0 + k)
    out = []
    for y in range(256):
        v = math.log(1.0 + k * y / 255.0) / denom
        out.append(max(0, min(255, int(round(v * 255.0)))))
    return out


def load_curve(path):
    vals = []
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        vals.append(int(line))
    if len(vals) != 19:
        raise SystemExit(f"{path}: 需要 19 个点，读到 {len(vals)}")
    if vals[0] != 0 or vals[-1] != 1023:
        raise SystemExit(f"{path}: 首尾必须是 0 / 1023")
    if any(b <= a for a, b in zip(vals, vals[1:])):
        raise SystemExit(f"{path}: 必须严格递增")
    # 19 点 → 256 项（等距，步长 1023/18）
    n = len(vals) - 1
    step = 1023.0 / n
    out = []
    for y in range(256):
        x = y * 1023.0 / 255.0
        i = min(int(x / step), n - 1)
        frac = (x - i * step) / step
        v = vals[i] + (vals[i + 1] - vals[i]) * frac
        out.append(max(0, min(255, int(round(v * 255.0 / 1023.0)))))
    return out


def apply_ev(lut256, ev):
    """整体曝光补偿：log 曲线必然抬亮中高调（凹函数 g(0.5)>0.5），
    不压回来就是"严重过曝"。ev 用 EV 表示，-0.7 ≈ ×0.62。"""
    if ev == 0:
        return lut256
    k = 2.0 ** ev
    return [max(0, min(255, int(round(v * k)))) for v in lut256]


def build_lut(lut256, chroma=1.0):
    """由 256 项映射表算局部增益（用于色度收缩）。"""
    gain = []
    for y in range(256):
        lo = lut256[max(0, y - 1)]
        hi = lut256[min(255, y + 1)]
        gain.append((hi - lo) / 2.0)
    return lut256, gain, chroma


def bake(src: pathlib.Path, dst: pathlib.Path, lut, gain, chroma):
    data = bytearray(src.read_bytes())
    if len(data) != SIZE:
        raise SystemExit(f"{src}: 大小 {len(data)} ≠ {SIZE}")
    ymin, ymax = 255, 0
    for e in range(0, SIZE, 4):
        cr, cb, y = data[e], data[e + 1], data[e + 2]
        ymin = min(ymin, y)
        ymax = max(ymax, y)
        ny = lut[y]
        data[e + 2] = ny
        if chroma != 1.0:
            # 以中性 128 为轴缩放色度：压缩暗部时避免颜色被"挤"得更艳。
            k = (gain[y] ** (1.0 - chroma))
            data[e] = max(0, min(255, int(round(128 + (cr - 128) * k))))
            data[e + 1] = max(0, min(255, int(round(128 + (cb - 128) * k))))
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(bytes(data))
    return ymin, ymax


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("-o", "--out")
    ap.add_argument("-d", "--outdir")
    ap.add_argument("--log", type=float, default=20.0,
                    help="解析 log 曲线的 k（默认 20；越大越平）")
    ap.add_argument("--curve", default=None,
                    help="改用 19 点 .tcy（注意：tc/*.tcy 是反 gamma 表，方向相反）")
    ap.add_argument("--ev", type=float, default=-0.7,
                    help="曝光补偿（EV）。log 会抬亮中高调，默认 -0.7 压回来")
    ap.add_argument("--chroma", type=float, default=1.0,
                    help="1.0=不动色度；<1 会随暗部压缩一起收色度")
    ap.add_argument("--prefix", default="vk_")
    a = ap.parse_args()
    if not a.out and not a.outdir:
        raise SystemExit("需要 -o 或 -d")

    lut256 = load_curve(a.curve) if a.curve else log_curve(a.log)
    lut256 = apply_ev(lut256, a.ev)
    lut, gain, chroma = build_lut(lut256, a.chroma)
    for p in a.inputs:
        src = pathlib.Path(p)
        dst = pathlib.Path(a.out) if a.out else \
            pathlib.Path(a.outdir) / (a.prefix + src.stem + ".bin")
        ymin, ymax = bake(src, dst, lut, gain, chroma)
        print(f"{src.name} -> {dst}  Y {ymin}..{ymax} -> "
              f"{lut[ymin]}..{lut[ymax]}")


if __name__ == "__main__":
    sys.exit(main())
