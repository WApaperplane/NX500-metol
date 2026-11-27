#!/usr/bin/env python3
"""Build fail-closed production and local-only test verifiers."""
from pathlib import Path
import glob
import subprocess

HERE = Path(__file__).resolve().parent
OUT = HERE / 'out'
TEST = OUT / 'test-only'
SOURCES = sorted((HERE / 'runtime').glob('*.c'))

def main():
    matches = glob.glob(str(Path.home() / 'nx-eye-venv/lib/python*/site-packages/ziglang/zig'))
    if len(matches) != 1:
        raise SystemExit('Expected exactly one zig executable in ~/nx-eye-venv')
    TEST.mkdir(parents=True, exist_ok=True)
    flags = ['-Wall', '-Wextra', '-Werror', '-O2', '-fno-pie', '-no-pie', '-Wl,-z,relro,-z,now']
    sources = [str(p) for p in SOURCES]
    arm = [matches[0], 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9']
    for command in (
        arm + flags + sources + ['-o', str(OUT / 'eye28-verify.arm')],
        arm + flags + ['-DTEST_BUILD'] + sources + ['-o', str(TEST / 'eye28-verify.arm')],
        ['cc'] + flags + ['-DTEST_BUILD'] + sources + ['-o', str(TEST / 'eye28-verify.host')],
    ):
        subprocess.run(command, check=True)

if __name__ == '__main__':
    main()
