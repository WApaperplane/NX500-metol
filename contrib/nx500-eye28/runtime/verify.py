"""Offline runtime wiring prototype. Never enables production deployment."""
import argparse, hashlib, json, os, stat
from pathlib import Path

BASE = Path(__file__).resolve().parent
TOKEN = '/tmp/nx-lut-lite/eye-af.resume-ready'

def inside(root, path):
    if not path.startswith('/') or '..' in Path(path).parts: raise ValueError('path')
    p = root / path.lstrip('/')
    cur = root
    for part in Path(path).parts[1:]:
        cur = cur / part
        if cur.is_symlink(): raise ValueError('symlink')
    return p

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def check(root, profile, pid, proof):
    if profile.get('schema') != 1 or profile.get('token') != TOKEN: return False
    if not profile.get('app') or not profile.get('libraries') or not profile.get('swap'): return False
    proc = inside(root, '/proc/' + str(pid))
    # Linux stat: comm is parenthesized and can contain spaces.
    statline = (proc/'stat').read_text(); end = statline.rfind(')')
    if end < 0: return False
    start = statline[end+2:].split()[19]
    boot = inside(root, '/proc/sys/kernel/random/boot_id').read_text().strip()
    if proof.get('pid') != pid or proof.get('starttime') != start or proof.get('boot_id') != boot: return False
    if not proof.get('generation') or proof.get('generation') != proof.get('image_generation'): return False
    if proof.get('build_id') != profile.get('build_id') or proof.get('report_id') != profile.get('report_id'): return False
    app = inside(root, profile['app']['path'])
    if digest(app) != profile['app']['sha256']: return False
    exe = proc/'exe'
    if not exe.is_symlink() or os.readlink(exe) != profile['app']['path']: return False
    maps = (proc/'maps').read_text().splitlines()
    for lib in profile['libraries']:
        disk = inside(root, lib['path']); st = disk.stat()
        if not stat.S_ISREG(st.st_mode) or digest(disk) != lib['sha256']: return False
        found = False
        for line in maps:
            fields=line.split(None, 5)
            if len(fields)==6 and fields[5]==lib['path'] and fields[4].isdigit() and int(fields[4])==st.st_ino:
                dev=fields[3].split(':')
                if len(dev)==2 and int(dev[0],16)==os.major(st.st_dev) and int(dev[1],16)==os.minor(st.st_dev): found=True
        if not found: return False
    swaps = inside(root, '/proc/swaps').read_text().splitlines()[1:]
    if not any(row.split() and row.split()[0] == profile['swap'] for row in swaps): return False
    token = inside(root, TOKEN)
    if token.exists(): return False  # stale or duplicate readiness is never reused
    return True

def main():
    cli=argparse.ArgumentParser(); cli.add_argument('operation', choices=['launch','resume']); cli.add_argument('--test-root'); cli.add_argument('--pid', type=int); cli.add_argument('--proof'); cli.add_argument('--profile')
    args=cli.parse_args()
    # No command-line option can enable the production path.
    if not args.test_root: print('NATIVE'); return
    try:
        root=Path(args.test_root).resolve(strict=True)
        profile=json.loads(Path(args.profile).read_text())
        proof=json.loads(Path(args.proof).read_text())
        print('CANDIDATE' if check(root, profile, args.pid, proof) else 'NATIVE')
    except (OSError, ValueError, KeyError, TypeError, IndexError, json.JSONDecodeError): print('NATIVE')

if __name__ == '__main__': main()
