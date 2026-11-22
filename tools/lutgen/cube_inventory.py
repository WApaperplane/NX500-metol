#!/usr/bin/env python3
# cube_inventory.py —— 目录里 .cube 的清点与分类（标准 / V-Log / RAW / 未知）
import sys, pathlib, re
root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/mnt/f/BaiduNetdiskDownload/lut")

def head(p, n=40):
    out = []
    with open(p, "r", errors="replace") as f:
        for i, line in enumerate(f):
            if i >= n: break
            out.append(line.rstrip("\n"))
    return out

def classify(path):
    txt = " ".join(head(path, 40)).lower()
    folder = str(path.parent).lower()
    if "vlog" in folder or "v-log" in folder: return "VLOG"
    if "vlog" in txt or "v-log" in txt: return "VLOG"
    if "raw" in folder or "raw" in txt.split("title")[-1][:80]: return "RAW"
    if "log" in txt: return "OTHER_LOG"
    return "STD"

rows = []
for p in sorted(root.rglob("*.cube")):
    h = head(p, 40)
    size = next((l.split()[-1] for l in h if l.startswith("LUT_3D_SIZE")), "?")
    title = next((l[6:].strip() for l in h if l.startswith("TITLE")), "")
    rng = next((l for l in h if "DOMAIN" in l or "RANGE" in l), "")
    rows.append((classify(p), size, p.parent.name[:26], p.name, title[:22], rng[:24]))

for cat in ("STD", "VLOG", "RAW", "OTHER_LOG"):
    sel = [r for r in rows if r[0] == cat]
    print(f"\n===== {cat}  {len(sel)} 个 =====")
    for r in sel:
        print(f"  [{r[1]:>3}] {r[2]:26s} | {r[3][:34]:34s} | {r[4][:22]:22s} | {r[5]}")
print(f"\nTOTAL {len(rows)}")
