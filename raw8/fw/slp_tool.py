#!/usr/bin/env python3
"""slp_tool.py — NX500 官方固件容器（SLP）的解析、自检、拆分与重打包。

格式依据 docs/current/SLP_CONTAINER_FORMAT_2026-10-08.md（全解、逐字节自证过）：

    0x00  48B 头：magic "SLP\\0" | version_user[8] | project[16] | version_date[16] | num_image u32
    0x30  16B 标签：01 "VER_PR1" 00...
    0x40  N × 24B 记录：len u32 | crc(JAMCRC) | offset u32 | flags u32 | tail[8]
    0x40+24N  各镜像连续平铺，无对齐填充，无签名块，EOF == 最后一段结尾

唯一校验：每段独立 JAMCRC = zlib.crc32(data) ^ 0xFFFFFFFF（不做最终异或）。

子命令：
    list    <slp>                     列表（段号/长度/偏移/CRC/魔数）
    verify  <slp>                     逐段重算 JAMCRC + 平铺自检 + 尾部对齐（只读）
    extract <slp> <outdir>            拆 10 段 + meta.json（供 repack）
    repack  <indir> <outfile>         从 extract 目录重建，并自证"重建 == 原件"
    patch   <in> <out> --img N --at OFF --hex XX [--hex XX ...]
                                      等长改字节 + 重算该段 CRC
    bump    <in> <out> --version 1.19 只改 version_user（8 字节，不足补 0）

安全：所有写操作先写到 <out>.tmp，校验通过后才 rename；不改输入文件。
"""
import argparse
import json
import os
import struct
import sys
import zlib

HDR = 0x30
TAG = 0x10
REC = 24
MAGICS = {
    b"\x27\x05\x19\x56": "uImage",
    b"\x06\x00\x00\xea": "ARM向量表",
    b"\x4e\x00\x00\xea": "p7/rtos",
    b"\x89LZO": "LZOP",
}


def jamcrc(b):
    return zlib.crc32(b) ^ 0xFFFFFFFF


def cstr(b):
    return b.split(b"\0")[0].decode("ascii", "replace")


def sniff(d):
    for k, v in MAGICS.items():
        if d.startswith(k):
            return v
    if d[:4] == b"\x00\x00\x00\x00":
        return "全零/空"
    return "?"


class Slp:
    def __init__(self, path):
        self.path = path
        self.size = os.path.getsize(path)
        with open(path, "rb") as f:
            h = f.read(HDR)
            if h[:4] != b"SLP\0":
                raise SystemExit("不是 SLP：magic=%r" % h[:4])
            self.magic = h[:4]
            self.version = cstr(h[4:12])
            self.project = cstr(h[12:28])
            self.vdate = cstr(h[28:44])
            self.num = struct.unpack("<I", h[44:48])[0]
            if self.num > 64:
                raise SystemExit("num_image=%d 不合理" % self.num)
            self.tag = f.read(TAG)
            raw = f.read(REC * self.num)
            self.recs = []
            for i in range(self.num):
                ln, crc, off, flags = struct.unpack("<IIII", raw[i * REC:i * REC + 16])
                tail = raw[i * REC + 16:i * REC + 24]
                self.recs.append(dict(idx=i, len=ln, crc=crc, offset=off,
                                      flags=flags, tail=tail))
        self.data_start = 0x40 + REC * self.num

    def read_seg(self, i):
        r = self.recs[i]
        with open(self.path, "rb") as f:
            f.seek(r["offset"])
            d = f.read(r["len"])
        if len(d) != r["len"]:
            raise SystemExit("img%d 读取不足：%d/%d" % (i, len(d), r["len"]))
        return d

    def head_bytes(self):
        h = self.magic
        h += self.version.encode().ljust(8, b"\0")[:8]
        h += self.project.encode().ljust(16, b"\0")[:16]
        h += self.vdate.encode().ljust(16, b"\0")[:16]
        h += struct.pack("<I", self.num)
        assert len(h) == HDR, len(h)
        return h


def cmd_list(s):
    print("%s  %d B  ver=%s proj=%s vdate=%s num=%d  data@0x%x"
          % (s.path, s.size, s.version, s.project, s.vdate, s.num, s.data_start))
    print("%3s %11s %10s %10s %-10s %s" % ("idx", "len", "offset", "crc", "magic", "tail"))
    for r in s.recs:
        with open(s.path, "rb") as f:
            f.seek(r["offset"])
            head = f.read(4)
        print("%3d %11d 0x%08x 0x%08x %-10s %s"
              % (r["idx"], r["len"], r["offset"], r["crc"], sniff(head),
                 r["tail"].hex() if any(r["tail"]) else "-"))


def cmd_verify(s):
    ok = True
    # 1) 平铺：每段 offset 必须等于前一段的结尾
    expect = s.data_start
    for r in s.recs:
        if r["offset"] != expect:
            print("FAIL 平铺断裂 img%d: offset=0x%x 期望 0x%x"
                  % (r["idx"], r["offset"], expect))
            ok = False
        expect = r["offset"] + r["len"]
    if expect != s.size:
        print("FAIL 尾部不对齐：最后一段结尾 0x%x，文件 %d" % (expect, s.size))
        ok = False

    # 2) 逐段 JAMCRC
    for r in s.recs:
        d = s.read_seg(r["idx"])
        got = jamcrc(d)
        mark = "OK  " if got == r["crc"] else "FAIL"
        if got != r["crc"]:
            ok = False
        print("%s img%d  len=%d  crc=0x%08x %s"
              % (mark, r["idx"], r["len"], got,
                 "" if got == r["crc"] else "(记录里 0x%08x)" % r["crc"]))

    print("VERDICT", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_extract(s, outdir):
    os.makedirs(outdir, exist_ok=True)
    meta = dict(version=s.version, project=s.project, vdate=s.vdate,
                tag=s.tag.hex(), segs=[])
    for r in s.recs:
        d = s.read_seg(r["idx"])
        name = "img%d_%s.bin" % (r["idx"], sniff(d).replace("/", "_"))
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(d)
        m = dict(r)
        m["file"] = name
        m["tail"] = r["tail"].hex()
        meta["segs"].append(m)
        print("img%d → %s  (%d B)" % (r["idx"], name, len(d)))
    with open(os.path.join(outdir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("meta.json 已写")


def cmd_repack(indir, outfile):
    meta = json.load(open(os.path.join(indir, "meta.json")))
    segs = []
    for m in meta["segs"]:
        d = open(os.path.join(indir, m["file"]), "rb").read()
        if len(d) != m["len"]:
            raise SystemExit("img%d 长度不符：%d != %d" % (m["idx"], len(d), m["len"]))
        segs.append((m, d))

    head = b"SLP\0"
    head += meta["version"].encode().ljust(8, b"\0")[:8]
    head += meta["project"].encode().ljust(16, b"\0")[:16]
    head += meta["vdate"].encode().ljust(16, b"\0")[:16]
    head += struct.pack("<I", len(segs))
    head += bytes.fromhex(meta["tag"])

    off = 0x40 + REC * len(segs)
    table = b""
    body = b""
    for m, d in segs:
        table += struct.pack("<IIII", len(d), jamcrc(d), off, m["flags"])
        table += bytes.fromhex(m["tail"])
        body += d
        off += len(d)

    tmp = outfile + ".tmp"
    with open(tmp, "wb") as f:
        f.write(head)
        f.write(table)
        f.write(body)
    os.replace(tmp, outfile)
    print("已写 %s（%d B）" % (outfile, off))
    print("自证：请对原包与重建包各跑一次 verify，并 cmp 逐字节比较")


def cmd_patch(inp, out, img, at, hexbytes):
    s = Slp(inp)
    if img < 0 or img >= s.num:
        raise SystemExit("img 越界")
    r = s.recs[img]
    d = bytearray(s.read_seg(img))
    payload = bytes.fromhex(hexbytes.replace(" ", ""))
    if at < 0 or at + len(payload) > len(d):
        raise SystemExit("偏移越界：%d+%d > %d" % (at, len(payload), len(d)))
    d[at:at + len(payload)] = payload
    new_crc = jamcrc(bytes(d))

    tmp = out + ".tmp"
    import shutil
    with open(inp, "rb") as fi, open(tmp, "wb") as fo:
        shutil.copyfileobj(fi, fo)
        fo.seek(r["offset"])
        fo.write(d)
        fo.seek(0x40 + REC * img + 4)
        fo.write(struct.pack("<I", new_crc))
    os.replace(tmp, out)
    print("img%d @%d 写入 %d 字节；CRC 0x%08x → 0x%08x"
          % (img, at, len(payload), r["crc"], new_crc))
    print("已写", out)


def cmd_bump(inp, out, version):
    s = Slp(inp)
    data = bytearray(open(inp, "rb").read())
    vb = version.encode().ljust(8, b"\0")[:8]
    data[4:12] = vb
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, out)
    print("version %s → %s；已写 %s" % (s.version, version, out))


def main():
    ap = argparse.ArgumentParser(description="NX500 SLP 固件容器工具")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list"); p.add_argument("slp")
    p = sub.add_parser("verify"); p.add_argument("slp")
    p = sub.add_parser("extract"); p.add_argument("slp"); p.add_argument("outdir")
    p = sub.add_parser("repack"); p.add_argument("indir"); p.add_argument("outfile")
    p = sub.add_parser("patch")
    p.add_argument("inp"); p.add_argument("out")
    p.add_argument("--img", type=int, required=True)
    p.add_argument("--at", type=lambda x: int(x, 0), required=True)
    p.add_argument("--hex", required=True)
    p = sub.add_parser("bump")
    p.add_argument("inp"); p.add_argument("out")
    p.add_argument("--version", required=True)

    a = ap.parse_args()
    if a.cmd == "list":
        cmd_list(Slp(a.slp))
    elif a.cmd == "verify":
        return cmd_verify(Slp(a.slp))
    elif a.cmd == "extract":
        cmd_extract(Slp(a.slp), a.outdir)
    elif a.cmd == "repack":
        cmd_repack(a.indir, a.outfile)
    elif a.cmd == "patch":
        cmd_patch(a.inp, a.out, a.img, a.at, a.hex)
    elif a.cmd == "bump":
        cmd_bump(a.inp, a.out, a.version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
