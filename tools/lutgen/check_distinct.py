#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""候选槽表 vs 全库的 ΔE00 去重（cube_import.py 只跟仓库 looks/ 比，那只有 89 张，
机上是 121 张 + vlog 10 张 —— 少比的部分会漏报重复）。

用法:
  python3 check_distinct.py _luts/lumix_bin
  python3 check_distinct.py _luts/lumix_bin --baseline looks,_luts/std,_luts/vlog
  python3 check_distinct.py _luts/lumix_bin --markdown      # 输出成表格

判据沿用 cube_import.py：dE00 > 3.112 才算 DISTINCT。
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

from similarity_audit import (apply_slot, de00, load_native,  # noqa: E402
                              probe_set, rgb_to_lab)

THRESHOLD = 3.112


def labs(directory: Path) -> dict:
    out = {}
    for p in sorted(directory.glob("*.bin")):
        try:
            view = load_native(p)
        except Exception:                                          # noqa: BLE001
            continue
        if view.shape[0] != 17:
            continue
        out[p.stem] = rgb_to_lab(apply_slot(probe_set()[0], view))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("candidate", help="候选 .bin 目录")
    ap.add_argument("--baseline", default="looks,_luts/std,_luts/vlog",
                    help="逗号分隔的基准目录（相对仓库根）")
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()

    cand = labs(Path(args.candidate))
    base = {}
    for d in args.baseline.split(","):
        d = d.strip()
        if d:
            base.update(labs(REPO / d))
    if not cand:
        print("no candidate .bin under %s" % args.candidate)
        return 2
    if not base:
        print("no baseline .bin — check --baseline")
        return 2

    rows = []
    for name, lab in sorted(cand.items()):
        d, near = min(((float(np.median(de00(lab, bl))), k) for k, bl in base.items()))
        rows.append((name, near, d, d > THRESHOLD))

    distinct = [r for r in rows if r[3]]
    print("baseline %d 张 · 候选 %d 张 · DISTINCT %d / DUPLICATE %d（门槛 %.3f）"
          % (len(base), len(rows), len(distinct), len(rows) - len(distinct), THRESHOLD))
    if args.markdown:
        print("\n| 名字 | 最近邻 | dE00 | |")
        print("|---|---|---|---|")
        for name, near, d, ok in rows:
            print("| `%s` | %s | %.2f | %s |" % (name, near, d, "✓" if ok else "重"))
    else:
        for name, near, d, ok in rows:
            print("%-18s %-22s %6.2f  %s" % (name, near, d, "DISTINCT" if ok else "DUPLICATE"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
