"""Measure the gallery thumbnail endpoint from the PC over Wi-Fi."""
import statistics
import sys
import time
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else 'http://192.168.0.108:8080/cgi-bin/thumb'
FILE = sys.argv[2] if len(sys.argv) > 2 else '100PHOTO/SAM_0476.JPG'


def fetch(path, width, quality=75, runs=3):
    url = '%s?f=%s&w=%d&q=%d' % (BASE, path, width, quality)
    times = []
    size = 0
    declared = None
    head = tail = b''
    for _ in range(runs):
        t0 = time.perf_counter()
        with urllib.request.urlopen(url, timeout=90) as resp:
            body = resp.read()
            declared = resp.headers.get('Content-Length')
            ct = resp.headers.get('Content-Type')
        times.append((time.perf_counter() - t0) * 1000.0)
        size = len(body)
        head, tail = body[:2], body[-2:]
    return {
        'median_ms': statistics.median(times),
        'all_ms': [round(t) for t in times],
        'size': size,
        'declared': declared,
        'ctype': ct,
        'whole_jpeg': head == b'\xff\xd8' and tail == b'\xff\xd9',
    }


for width in (320, 1024):
    r = fetch(FILE, width)
    ok = 'ok' if (r['whole_jpeg'] and r['declared'] == str(r['size'])) else 'BAD'
    print('w=%-5d %6.0f ms  %7d bytes  declared=%-7s %-11s %s  runs=%s'
          % (width, r['median_ms'], r['size'], r['declared'], r['ctype'], ok, r['all_ms']))
