"""One-shot timing for a single thumbnail request (cold vs warm)."""
import sys
import time
import urllib.request

base = 'http://192.168.0.108:8080/cgi-bin/thumb'
path = sys.argv[2] if len(sys.argv) > 2 else '100PHOTO/SAM_0476.JPG'
width = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
label = sys.argv[3] if len(sys.argv) > 3 else ''

url = '%s?f=%s&w=%d&q=85' % (base, path, width)
t0 = time.perf_counter()
try:
    with urllib.request.urlopen(url, timeout=120) as resp:
        body = resp.read()
        declared = resp.headers.get('Content-Length')
        ctype = resp.headers.get('Content-Type')
    dt = (time.perf_counter() - t0) * 1000.0
    whole = body[:2] == b'\xff\xd8' and body[-2:] == b'\xff\xd9'
    print('%-22s w=%-5d %8.0f ms  %8d B  declared=%-8s %s  jpeg=%s'
          % (label, width, dt, len(body), declared, ctype, whole))
except Exception as exc:  # noqa: BLE001
    print('%-22s w=%-5d FAILED after %.0f ms: %s'
          % (label, width, (time.perf_counter() - t0) * 1000.0, exc))
    sys.exit(1)
