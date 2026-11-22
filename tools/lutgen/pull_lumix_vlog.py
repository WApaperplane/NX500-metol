#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 changyun233/Lumix-V-log-LUTs 的 V-Log 表烘成「Standard 输入」的相机表。

为什么需要这一步
----------------
那个库的表输入域是 **V-Log 编码**（18% 灰 = 0.42、黑位 0.098 以上），而 NX500
的 A18 3D LUT 槽吃的是 **相机 ISP 输出的显示参考流**（JPEG 域，前面没有对数
解码）。直接把 V-Log 表写进槽 = 双重错位：表以为素材很暗，实际很正常 ⇒ 严重
欠曝 + 发灰。这条结论见 cube_import.py 的开头。

所以先做前置复合：

    T_std(x) = T_vlog( S2V(x) )

S2V = 「Standard → V-Log」转换表（`_lutweb/vla/S5IIS2V.cube`，Panasonic S5II
官方 Standard↔V-Log 的前向拟合）。`merge_standard_luts.py` 负责这一步。

分组选择（为什么不整库拉）
--------------------------
  · varicam_official        35  官方影视 look（Bleach Bypass / BandW 三档 / Hi-LoCon …）
  · lumix_color_lab_A/B/C   22  Panasonic 色彩实验室
  · 不拉纯转换表：VLog_to_V709、Classic_Rec709/Rec2020、Natural_Rec709/Rec2020
    —— 它们输出仍是 log 编码（实测 0.42→0.418），烘完只会得到灰片。
  · 不拉作者胶片那批：与现有 121 张风格重叠，等这条链路验证完再议。

命名
----
`merge_standard_luts.py` 要求 1-8 位 ASCII 字母数字（Panasonic 机内 LUT 名的
限制），所以烘焙阶段用 v01..v35 / l01..l22，导入前再复制成可读名（vc_* / lcl_*）。

用法
----
  python3 pull_lumix_vlog.py --download          # 只下载到 _luts/lumix_src
  python3 pull_lumix_vlog.py --bake              # 只烘焙（缺源会先下载）
  python3 pull_lumix_vlog.py                     # 下载 + 烘焙 + 复制成可读名
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]                       # nx500-modding-work/
SRC = REPO / "_luts" / "lumix_src"
BAKED = REPO / "_luts" / "lumix_std"
NAMED = REPO / "_luts" / "lumix_std_named"
VLOG_NAMED = REPO / "_luts" / "lumix_vlog_named"
RAW_BASE = "https://raw.githubusercontent.com/changyun233/Lumix-V-log-LUTs/main/"
MERGE = Path("/mnt/e/NXWA/_lutweb/vla/merge_standard_luts.py")

VARICAM = [
    ("Agressive_709", "vc_agressive"),
    ("BandW_HiCon", "vc_bw_hicon"),
    ("BandW_LoCon Dark", "vc_bw_locondk"),
    ("BandW_LoCon", "vc_bw_locon"),
    ("BandW_MidCon", "vc_bw_midcon"),
    ("BleachyBypass_LoCon", "vc_bleach_lo"),
    ("BleachyBypass_SuperBright", "vc_bleach_sb"),
    ("BlueNight", "vc_bluenight"),
    ("Blue_Dusk", "vc_bluedusk"),
    ("Cyan_1", "vc_cyan1"),
    ("Cyan_2", "vc_cyan2"),
    ("Fashion_1", "vc_fashion1"),
    ("Fashion_2", "vc_fashion2"),
    ("Fashion_Lo Con", "vc_fashionlo"),
    ("Golden_1", "vc_golden1"),
    ("Golden_2", "vc_golden2"),
    ("Hangover", "vc_hangover"),
    ("HiCon", "vc_hicon"),
    ("LoCon_Cool", "vc_locool"),
    ("LoCon_Neu", "vc_loneu"),
    ("LoCon_Warm", "vc_lowarm"),
    ("Magenta_1", "vc_magenta1"),
    ("Magenta_2", "vc_magenta2"),
    ("Matrix_1", "vc_matrix1"),
    ("Matrix_2", "vc_matrix2"),
    ("Morning_Light", "vc_morning"),
    ("Nicest_709", "vc_nicest"),
    ("Rose_1", "vc_rose1"),
    ("Rose_2", "vc_rose2"),
    ("Sepia_1", "vc_sepia"),
    ("SteelBlue", "vc_steelblue"),
    ("StraitYellow", "vc_straityel"),
    ("Vintage", "vc_vintage"),
    ("WarmDawn", "vc_warmdawn"),
    ("Winter", "vc_winter"),
]

COLOR_LAB = [
    ("A/HollywoodBlue_Day", "lcl_hollyday"),
    ("A/HollywoodBlue_Night", "lcl_hollynit"),
    ("A/cinebasemodeA_33", "lcl_cinebasea"),
    ("A/desert_33", "lcl_desert"),
    ("A/dramaticviolet_33", "lcl_dramavio"),
    ("A/sengo_33", "lcl_sengo"),
    ("B/Dramatic_BlockBuster_33", "lcl_blockbust"),
    ("B/Majestic_Noir_33", "lcl_noir"),
    # 名字上限 12 字符：面板列表 5 格、每格约 133 px，19px 字体下 13 个字符
    # 就会被裁掉尾巴（实测 `lcl_nostblea`）。所以 "Nostalgic_BleachByPass"
    # 缩成 lcl_bleach，而不是 lcl_nostbleach。
    ("B/Nostalgic_BleachByPass_33", "lcl_bleach"),
    ("B/Realistic_Cinetone_33", "lcl_cinetone"),
    ("B/Realistic_Filmtone_33", "lcl_filmtone"),
    ("B/Romantic_Memory_33", "lcl_romantic"),
    ("B/Selen_Chrome_33", "lcl_selenchr"),
    ("B/Silky_Chrome_33", "lcl_silkychr"),
    ("C/S_Deep_Forest_1", "lcl_forest1"),
    ("C/S_Deep_Forest_2", "lcl_forest2"),
    ("C/S_Golden_Hour_1", "lcl_goldenh1"),
    ("C/S_Golden_Hour_2", "lcl_goldenh2"),
    ("C/S_Night_Time_1", "lcl_night1"),
    ("C/S_Night_Time_2", "lcl_night2"),
    ("C/S_Teal_Orange_1", "lcl_tealor1"),
    ("C/S_Teal_Orange_2", "lcl_tealor2"),
]


def entries():
    """(远端相对路径, 本地短名, 可读名) 的完整清单。"""
    out = []
    for i, (name, readable) in enumerate(VARICAM, 1):
        out.append(("luts/varicam_official/%s.cube" % name, "v%02d" % i, readable))
    for i, (name, readable) in enumerate(COLOR_LAB, 1):
        out.append(("luts/lumix_color_lab_%s.cube" % name, "l%02d" % i, readable))
    return out


def download(force: bool = False) -> int:
    SRC.mkdir(parents=True, exist_ok=True)
    got = 0
    for rel, short, _ in entries():
        dst = SRC / ("%s.cube" % short)
        if dst.exists() and dst.stat().st_size > 100000 and not force:
            continue
        url = RAW_BASE + urllib.parse.quote(rel)
        try:
            data = urllib.request.urlopen(url, timeout=40).read()
        except Exception as exc:                                  # noqa: BLE001
            print("  FAIL %-44s %s" % (rel, exc))
            continue
        dst.write_bytes(data)
        print("  %-8s %7d B  <- %s" % (short, len(data), rel.split("/")[-1]))
        got += 1
    print("downloaded %d, cached %d" % (got, len(entries()) - got))
    return got


def bake() -> int:
    BAKED.mkdir(parents=True, exist_ok=True)
    missing = [s for _, s, _ in entries() if not (SRC / ("%s.cube" % s)).exists()]
    if missing:
        print("missing sources: %s (run --download first)" % ", ".join(missing))
        return 2
    done = 0
    for _, short, readable in entries():
        out = BAKED / ("%s.cube" % short)
        if out.exists() and out.stat().st_size > 100000:
            done += 1
            continue
        cmd = [sys.executable, str(MERGE), "--model", "S5II",
               "--lut1", str(SRC / ("%s.cube" % short)),
               "--output-dir", str(BAKED), "--name1", short, "--size", "33"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print("  BAKE FAIL %-8s %s" % (short, (r.stdout + r.stderr).strip()[-200:]))
            continue
        print("  %-8s -> %s" % (short, readable))
        done += 1
    print("baked %d/%d" % (done, len(entries())))
    return 0 if done == len(entries()) else 1


def name_copies() -> int:
    """烘出来的 8 字符名复制成可读名，供 cube_import 生成相机 look 名。"""
    NAMED.mkdir(parents=True, exist_ok=True)
    n = 0
    for _, short, readable in entries():
        src = BAKED / ("%s.cube" % short)
        if not src.exists():
            continue
        dst = NAMED / ("%s.cube" % readable)
        shutil.copyfile(src, dst)
        n += 1
    table = {readable: {"short": short, "source": rel}
             for rel, short, readable in entries()}
    (NAMED / "map.json").write_text(json.dumps(table, ensure_ascii=False, indent=1),
                                    encoding="utf-8")
    print("named %d -> %s" % (n, NAMED))
    return 0


def vlog_copies() -> int:
    """原 V-Log 表另留一份，给 `_vlog/` 域用（页签选 V-Log gamma 时才有意义）。

    ★ 名字必须跟标准版不同：`lut.sh::table_of` 先查 `looks/`、查不到才查
      `looks/_vlog/`，同名会永远命中标准版 —— 想让面板拿到 log 版就再也拿不到。
      所以加 `vl_` 前缀（`vc_bleach_sb` → `vl_bleach_sb`）。
    """
    VLOG_NAMED.mkdir(parents=True, exist_ok=True)
    n = 0
    for _, short, readable in entries():
        src = SRC / ("%s.cube" % short)
        if not src.exists():
            continue
        base = readable.split("_", 1)[1] if "_" in readable else readable
        shutil.copyfile(src, VLOG_NAMED / ("vl_%s.cube" % base))
        n += 1
    print("vlog copies %d -> %s" % (n, VLOG_NAMED))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--bake", action="store_true")
    ap.add_argument("--vlog", action="store_true",
                    help="只生成 _vlog/ 域用的原表副本（不烘焙）")
    ap.add_argument("--force", action="store_true", help="重新下载")
    args = ap.parse_args()
    if args.download:
        download(args.force)
        return 0
    if args.bake:
        return bake()
    if args.vlog:
        return vlog_copies()
    download(args.force)
    rc = bake()
    name_copies()
    vlog_copies()
    return rc


if __name__ == "__main__":
    sys.exit(main())
