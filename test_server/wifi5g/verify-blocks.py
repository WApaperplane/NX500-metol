import struct, hashlib

b = open('sd8887_uapsta.bin', 'rb').read()


def hdr(o):
    if o < 0 or o + 16 > len(b):
        return None
    t, addr, ln, ck = struct.unpack_from('<4I', b, o)
    if t != 1 or not (0x100 < ln <= 0x1000):
        return None
    if not (0x1000 <= addr < 0x200000):
        return None
    if o + 16 + ln > len(b):
        return None
    return addr, ln


# 已知两块头作为种子；按 16+len 步长前后串成链，最多各走 200 步防跑飞
seeds = [0x84598, 0x849a8]
blocks = {}
for s in seeds:
    o = s
    for _ in range(200):
        h = hdr(o)
        if not h:
            break
        blocks[o] = h
        o = o + 16 + h[1]
    o = s
    for _ in range(200):
        # 向前：块的结束位置要么等于 o，要么 o+4（4 字节重叠）
        prev = None
        for cand in (o - 0x404 - 16, o - 0x400 - 16):
            if cand >= 0:
                h = hdr(cand)
                if h and cand + 16 + h[1] in (o, o + 4):
                    prev = (cand, h)
                    break
        if not prev:
            break
        blocks[prev[0]] = prev[1]
        o = prev[0]

offs = sorted(blocks)
print('块数:', len(offs))

mem = {}
for o in offs:
    addr, ln = blocks[o]
    for i in range(ln):
        mem[addr + i] = b[o + 16 + i]      # 后块覆盖重叠
print('覆盖 0x%x..0x%x' % (min(mem), max(mem)))

START = 0x42E2B
tab = bytes(mem.get(START + i, 0xFF) for i in range(663))
chs = [tab[i * 17] for i in range(39)]
print('信道:', chs)
print('ch36 :', list(tab[1:17]))
print('ch183:', list(tab[24 * 17 + 1:24 * 17 + 17]))

for fn in ['sd8887_uapsta.bin.compact-5g.bin',
           'sdio8887_sdio_combo.bin.compact-5g.bin']:
    d = open(fn, 'rb').read()
    print(fn[-30:], len(d), hashlib.sha256(d).hexdigest()[:16],
          '相同' if d == tab else '不同')
