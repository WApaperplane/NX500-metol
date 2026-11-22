#!/usr/bin/env python3
"""Run after pressing REC on the camera. Press REC again to stop afterwards.

python3 test_server/bench-record-concurrency.py --host 192.168.0.108
Use --no-record for the idle baseline or --scene a for the grid alone.
"""
import argparse
from contextlib import contextmanager
import math
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import signal
import statistics
import subprocess
import sys
from threading import Event, Thread
import time
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
SSH = ["bash", "tools/nx-ssh.sh", "cmd"]
DCIM = "/mnt/mmc/DCIM"
CONVERT = "/opt/usr/nx-ks/nx-rc/tools/usr/bin/convert"
CACHE = "/mnt/mmc/.thumbcache/1024x85"
MEMORY = r"""awk '/^MemFree:/ {f=$2} /^Cached:/ {c=$2}
    END {if (f == "" || c == "") exit 1; print f, c}' /proc/meminfo"""

RECORDING = r"""BB=/opt/usr/nx-ks/busybox
pids=$($BB pidof di-camera-app) || exit 3
for pid in $pids; do
    for fd in /proc/$pid/fd/*; do
        file=$(readlink "$fd" 2>/dev/null) || continue
        case "$file" in
            */DCIM/*.[mM][pP]4)
                flags=$($BB awk '/^flags:/ {print $2; exit}' "/proc/$pid/fdinfo/${fd##*/}" 2>/dev/null)
                case "$flags" in ""|*[!0-7]*) continue ;; esac
                if [ $((flags & 3)) -ne 0 ]; then
                    printf '%s\n' "$file"
                    exit 0
                fi
                ;;
        esac
    done
done"""


def ssh_env(host):
    return dict(os.environ, NXHOST=host)


def sh(host, script, timeout=15):
    result = subprocess.run(SSH + [script], cwd=ROOT, env=ssh_env(host),
                            capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"camera command failed (rc={result.returncode})")
    return result.stdout.strip()


def fetch(host, relative, width, quality, expect_slice):
    query = urllib.parse.urlencode(dict(f=relative, w=width, q=quality))
    start = time.perf_counter()
    with urllib.request.urlopen(f"http://{host}:8080/cgi-bin/thumb?{query}", timeout=45) as response:
        body = response.read()
        declared = response.headers.get("Content-Length")
        if response.headers.get_content_type() != "image/jpeg":
            raise RuntimeError(body[:180].decode(errors="replace").strip())
    if not body.startswith(b"\xff\xd8") or not body.endswith(b"\xff\xd9"):
        raise RuntimeError("thumbnail is not a complete JPEG")
    if declared is not None and int(declared) != len(body):
        raise RuntimeError("thumbnail Content-Length does not match the body")
    # The current CGI sets Content-Length only for the embedded JPEG path.
    if (declared is not None) != expect_slice:
        raise RuntimeError("thumbnail CGI used an unexpected processing path")
    return (time.perf_counter() - start) * 1000


@contextmanager
def cold_fixture(host, relative):
    src = shlex.quote(f"{DCIM}/{relative}")
    staging = f"""set -e
d=$(/opt/usr/nx-ks/busybox mktemp -d {DCIM}/.nxthumb-bench.XXXXXX)
trap 'rm -f "$d/frame.png"; rmdir "$d"' EXIT
cp {src} "$d/frame.png"
trap - EXIT
printf '%s\\n' "$d"
"""
    directory = sh(host, staging, timeout=45)
    if not re.fullmatch(r"/mnt/mmc/DCIM/\.nxthumb-bench\.[A-Za-z0-9]+", directory):
        raise RuntimeError("camera returned an unexpected benchmark directory")
    try:
        # The extension forces CGI fallback. The converter reads JPEG magic
        # and uses the same scaled JPEG decoding as a normal fallback.
        yield directory[len(DCIM) + 1:] + "/frame.png"
    finally:
        cache = f"{CACHE}/{PurePosixPath(directory).name}"
        sh(host, f'rm -f {cache}/frame.png.jpg {directory}/frame.png; '
                 f'[ ! -d {cache} ] || rmdir {cache}; rmdir {directory}')


@contextmanager
def sample_memory(host, floor_kb, fallback):
    stop = ":"
    if fallback:
        argument = shlex.quote(f"{DCIM}/{fallback}[0]")
        stop = f"""
        for pid in $($BB pidof convert); do
            [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = {CONVERT} ] || continue
            if tr '\\000' '\\n' < "/proc/$pid/cmdline" | grep -Fx {argument} >/dev/null; then
                kill -TERM "$pid" 2>/dev/null || :
            fi
        done
"""
    script = f"""BB=/opt/usr/nx-ks/busybox
i=0
low=0
while [ "$i" -lt 180 ]; do
    pair=$({MEMORY}) || exit 1
    printf '%s\\n' "$pair" || exit 1
    set -- $pair
    if [ $(( $1 + $2 )) -lt {floor_kb} ]; then low=1; fi
    if [ "$low" -eq 1 ]; then
        {stop}
    fi
    "$BB" sleep 0.5
    i=$((i+1))
done
"""
    samples = []
    ready, below_floor = Event(), Event()
    posix_session = os.name == "posix"
    proc = subprocess.Popen(SSH + [script], cwd=ROOT, env=ssh_env(host),
                            stdout=subprocess.PIPE, text=True,
                            start_new_session=posix_session)

    def collect():
        with proc.stdout:
            for line in proc.stdout:
                fields = line.split()
                if len(fields) == 2 and all(value.isdecimal() for value in fields):
                    free, cached = map(int, fields)
                    samples.append((free, cached))
                    if free + cached < floor_kb:
                        below_floor.set()
                    ready.set()

    def stop_sampler(sig):
        try:
            if posix_session:
                os.killpg(proc.pid, sig)
            elif sig == signal.SIGTERM:
                proc.terminate()
            else:
                proc.kill()
        except ProcessLookupError:
            pass

    reader = Thread(target=collect, daemon=True)
    reader.start()
    try:
        if not ready.wait(12):
            raise RuntimeError("camera memory sampler did not start")
        yield samples, below_floor, proc
    finally:
        if proc.poll() is None or (posix_session and reader.is_alive()):
            stop_sampler(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                stop_sampler(signal.SIGKILL if posix_session else None)
                proc.wait(timeout=3)
        if posix_session and reader.is_alive():
            reader.join(timeout=1)
            if reader.is_alive():
                stop_sampler(signal.SIGKILL)
        reader.join(timeout=3)


def measure(host, paths, width, quality, floor_kb, recording, label, fallback=None):
    if sh(host, RECORDING) != recording:
        raise RuntimeError(f"{label}: recording changed before the measurement")
    times = []
    with sample_memory(host, floor_kb, fallback) as (samples, below_floor, proc):
        for path in paths:
            if below_floor.is_set() or proc.poll() is not None:
                raise RuntimeError(f"{label}: memory floor crossed or sampler stopped")
            try:
                times.append(fetch(host, path, width, quality, expect_slice=fallback is None))
            except (OSError, RuntimeError) as error:
                if below_floor.is_set():
                    raise RuntimeError(f"{label}: memory floor crossed") from error
                raise
        if below_floor.is_set() or proc.poll() is not None:
            raise RuntimeError(f"{label}: memory floor crossed or sampler stopped")
    if sh(host, RECORDING) != recording:
        raise RuntimeError(f"{label}: recording changed during the measurement")
    times.sort()
    free, cached = min(samples, key=sum)
    p95 = times[math.ceil(len(times) * 0.95) - 1]
    print(f"{label}: n={len(times)} p50={statistics.median(times):.0f} ms "
          f"p95={p95:.0f} ms max={times[-1]:.0f} ms; "
          f"MemFree+Cached low={free+cached} KiB (free={free}, cached={cached})",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.0.108")
    parser.add_argument("--dir", default="100PHOTO")
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--scene", default="ab", choices=["a", "b", "ab", "none"])
    parser.add_argument("--no-record", action="store_true", help="require an idle camera for the baseline")
    parser.add_argument("--floor-mb", type=int, default=40, help="minimum MemFree + Cached in MiB")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.dir):
        parser.error("--dir must be one DCIM directory name")
    if args.count < 1 or args.floor_mb < 1:
        parser.error("--count and --floor-mb must be positive")

    host = args.host
    recording = sh(host, RECORDING)
    if not args.no_record and not recording:
        raise RuntimeError("camera is idle; press REC on the camera, then run again")
    if args.no_record and recording:
        raise RuntimeError("camera is recording; stop at the camera before the idle baseline")
    print(f"camera {host}, mode={'active recording' if recording else 'idle'}", flush=True)
    print(f"baseline MemFree Cached (KiB): {sh(host, MEMORY)}", flush=True)
    if args.scene == "none":
        return 0

    listing = sh(host, f"ls -1 {DCIM}/{args.dir}/*.[jJ][pP][gG] 2>/dev/null | head -n {args.count}")
    frames = [f"{args.dir}/{PurePosixPath(name).name}" for name in listing.splitlines()]
    if not frames:
        raise RuntimeError(f"no JPEG photos under DCIM/{args.dir}")

    floor_kb = args.floor_mb * 1024
    if "a" in args.scene:
        measure(host, frames, 320, 75, floor_kb, recording, "grid thumbnails")
    if "b" in args.scene:
        with cold_fixture(host, frames[0]) as fallback:
            measure(host, [fallback], 1024, 85, floor_kb, recording, "cold lightbox fallback", fallback)
    print("PASS", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        sys.exit(f"Benchmark stopped: {error}")
