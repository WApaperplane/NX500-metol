#!/usr/bin/env python3
"""Regression test for nxthumb.arm.

The camera stores two derived JPEGs in every still. This test treats an
independent Python parse of the same file as ground truth and asserts that the
ARM binary reproduces those byte ranges exactly -- including the 36-byte APP1
it injects for portrait frames.

Run from the repo root:
    python3 test_server/jpegthumb/test_nxthumb.py [--arm]

Without --arm it exercises the same logic through the build artifact under
qemu-arm-static, which is the ABI the camera will actually see.
"""
import glob
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(HERE, 'build', 'nxthumb.arm')
QEMU = ['qemu-arm-static'] if os.path.exists('/usr/bin/qemu-arm-static') else []
SAMPLES = sorted(glob.glob(os.path.join(ROOT, '..', '_photos', '*.JPG')))

APP1_ORIENT_PREFIX = bytes([
    0xFF, 0xE1, 0x00, 0x22, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00,
    0x4D, 0x4D, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01,
    0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00,
])
APP1_ORIENT_TAIL = bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x00])


def segments(d):
    out = []
    i = 2
    for _ in range(64):
        if i + 4 > len(d) or d[i] != 0xFF:
            break
        m = d[i + 1]
        if m in (0xD8, 0xD9) or 0xD0 <= m <= 0xD7:
            i += 2
            continue
        ln = struct.unpack('>H', d[i + 2:i + 4])[0]
        out.append((m, i, ln))
        if m == 0xDA:
            break
        i += 2 + ln
    return out


def parse(d):
    r = {'orientation': 1, 'thumb': None, 'mpf_size': 0, 'mpf_hint': None}
    for m, o, ln in segments(d):
        payload = d[o + 4:o + 2 + ln]
        if m == 0xE1 and payload[:6] == b'Exif\x00\x00':
            t = payload[6:]
            base = o + 10
            bo = '>' if t[:2] == b'MM' else '<'
            ifd0 = struct.unpack(bo + 'I', t[4:8])[0]
            n0 = struct.unpack(bo + 'H', t[ifd0:ifd0 + 2])[0]
            for k in range(n0):
                e = ifd0 + 2 + k * 12
                if struct.unpack(bo + 'H', t[e:e + 2])[0] == 0x0112:
                    r['orientation'] = struct.unpack(bo + 'H', t[e + 8:e + 10])[0]
            nxt = struct.unpack(bo + 'I', t[ifd0 + 2 + n0 * 12:ifd0 + 6 + n0 * 12])[0]
            if nxt:
                n1 = struct.unpack(bo + 'H', t[nxt:nxt + 2])[0]
                th = {}
                for k in range(n1):
                    e = nxt + 2 + k * 12
                    tag = struct.unpack(bo + 'H', t[e:e + 2])[0]
                    if tag in (0x0201, 0x0202):
                        th[tag] = struct.unpack(bo + 'I', t[e + 8:e + 12])[0]
                if 0x0201 in th and 0x0202 in th:
                    r['thumb'] = (base + th[0x0201], th[0x0202])
        if m == 0xE2 and payload[:4] == b'MPF\x00':
            t = payload[4:]
            if t[:2] != b'MM':
                continue
            for k in range(struct.unpack('>H', t[8:10])[0]):
                q = 10 + k * 12
                if struct.unpack('>H', t[q:q + 2])[0] != 0xB002:
                    continue
                v = struct.unpack('>I', t[q + 8:q + 12])[0]
                if struct.unpack('>I', t[q + 4:q + 8])[0] // 16 > 1:
                    mp = t[v + 16:v + 32]
                    r['mpf_size'] = struct.unpack('>I', mp[4:8])[0]
                    r['mpf_hint'] = (o + 4) + struct.unpack('>I', mp[8:12])[0]
    return r


def unwrap(d, hint):
    """Independent walk: the appended image wraps the real stream in a tiny Exif."""
    pos = max(hint - 8, 0)
    for _ in range(4):
        soi = d.find(b'\xff\xd8', pos)
        if soi < 0:
            return None
        k = soi + 2
        again = None
        for _ in range(8):
            if k + 3 >= len(d) or d[k] != 0xFF:
                break
            m = d[k + 1]
            if m == 0xD8:
                again = k
                break
            if m == 0xDA:
                return soi
            ln = struct.unpack('>H', d[k + 2:k + 4])[0]
            if ln < 2:
                break
            k += 2 + ln
        if again is None:
            return soi
        pos = again
    return None


def run(verb, path):
    p = subprocess.run(QEMU + [BIN, verb, path], capture_output=True)
    return p.returncode, p.stdout


def expected(gold, orientation):
    if orientation > 1 and orientation <= 8:
        return b'\xff\xd8' + APP1_ORIENT_PREFIX + bytes([orientation]) + APP1_ORIENT_TAIL + gold[2:]
    return gold


def main():
    if not os.path.exists(BIN):
        print('missing %s -- run build_nxthumb.sh first' % BIN)
        return 1
    if not SAMPLES:
        print('no samples under ../_photos')
        return 1

    fails = 0
    checked = 0
    orient_seen = set()
    for f in SAMPLES:
        if '_thumb' in f:
            continue
        d = open(f, 'rb').read()
        r = parse(d)
        name = os.path.basename(f)

        rc, out = run('info', f)
        fields = out.decode().split()
        if rc != 0 or len(fields) != 7:
            print('FAIL %-16s info rc=%d out=%r' % (name, rc, out[:60]))
            fails += 1
            continue
        tl, orient, bl = (int(fields[0]), int(fields[1]), int(fields[2]))
        if orient != r['orientation']:
            print('FAIL %-16s orientation %d != python %d' % (name, orient, r['orientation']))
            fails += 1
            continue
        orient_seen.add(orient)

        if r['thumb']:
            if tl != r['thumb'][1]:
                print('FAIL %-16s thumb len %d != %d' % (name, tl, r['thumb'][1]))
                fails += 1
            off, ln = r['thumb']
            gold = expected(d[off:off + ln], orient)
            rc, out = run('thumb', f)
            if rc != 0 or out != gold:
                print('FAIL %-16s thumb rc=%d len=%d gold=%d equal=%s'
                      % (name, rc, len(out), len(gold), out == gold))
                fails += 1
            else:
                checked += 1

        if r['mpf_hint']:
            inner = unwrap(d, r['mpf_hint'])
            if inner:
                raw = len(d) - inner  # what the slicer reads from the card
                gold = expected(d[inner:], orient)
                if bl != raw:
                    print('FAIL %-16s big len %d != on-card %d' % (name, bl, raw))
                    fails += 1
                rc, out = run('big', f)
                if rc != 0 or out != gold:
                    print('FAIL %-16s big rc=%d len=%d gold=%d equal=%s'
                          % (name, rc, len(out), len(gold), out == gold))
                    fails += 1
                else:
                    checked += 1

    print('\n%d byte-exact slices verified, %d failures, orientations seen: %s'
          % (checked, fails, sorted(orient_seen)))
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
