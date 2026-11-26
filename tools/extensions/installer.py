"""Offline NX500 extension staging. Never performs camera installation."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import uuid


def volume_identity(path):
    if os.name != 'nt':
        raise ValueError('Windows volume identity required')
    root = Path(path).resolve().anchor
    serial = ctypes.c_ulong()
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    get_volume = kernel.GetVolumeInformationW
    get_volume.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.c_wchar_p, ctypes.c_ulong]
    get_volume.restype = ctypes.c_int
    if not get_volume(root, None, 0, ctypes.byref(serial), None, None, None, 0):
        raise OSError('Cannot read Windows volume serial')
    return f'{root.upper()}:{serial.value:08X}'


def safe_file(root, name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '\\' in name or ':' in name or any(x in ('', '.', '..') for x in name.split('/')):
        raise ValueError('Unsafe relative path: ' + name)
    cur = root
    for part in p.parts:
        cur = cur / part
        if cur.is_symlink():
            raise ValueError('Symlink forbidden: ' + str(cur))
    if not cur.is_file():
        raise ValueError('Missing file: ' + str(cur))
    return cur


def inspect(manifest, payload):
    m = json.loads(Path(manifest).read_text(encoding='utf-8'))
    if m.get('schema_version') != 1 or m.get('model') != 'NX500' or m.get('firmware') != '1.12' or not m.get('profile') or not m.get('upstream'):
        raise ValueError('Invalid manifest identity')
    target = m.get('target_root', '')
    targets = {'log117': '/opt/usr/nx-ks/gamma', 'eye-yunet32': '/usr/local/lib/nx-eye-af'}
    if m.get('profile') not in targets or target != targets[m['profile']]:
        raise ValueError('Invalid target_root')
    if Path(payload).is_symlink() or not Path(payload).is_dir():
        raise ValueError('Invalid payload directory')
    seen = set()
    for entry in m.get('files', []):
        name = entry['path']
        if name in seen:
            raise ValueError('Duplicate file')
        seen.add(name)
        mode = entry['mode']
        if not isinstance(mode, (str, int)) or int(str(mode), 8) & ~0o775:
            raise ValueError('Invalid mode')
        digest = hashlib.sha256(safe_file(Path(payload), name).read_bytes()).hexdigest()
        if digest != entry['sha256']:
            raise ValueError('Hash mismatch: ' + name)
    if not seen:
        raise ValueError('Empty manifest')
    return m


def stage(manifest, payload, sd, expected_volume, provider=volume_identity):
    m = inspect(manifest, payload)
    sd = Path(sd)
    if sd.is_symlink() or not sd.is_dir() or provider(sd) != expected_volume:
        raise ValueError('SD volume identity mismatch')
    if any((sd / name).exists() or (sd / name).is_symlink() for name in ('info.tg', 'install.sh', 'nx_cs.adj')):
        raise ValueError('Existing trigger entry')
    base = sd / '.nxwa-extensions'
    if base.is_symlink():
        raise ValueError('Symlink transaction directory')
    base.mkdir(exist_ok=True)
    for existing in base.iterdir():
        if existing.is_symlink():
            raise ValueError('Symlink transaction')
        if existing.is_dir() and (existing / 'manifest.json').is_file():
            if json.loads((existing / 'manifest.json').read_text(encoding='utf-8')) == m:
                raise ValueError('Payload already staged')
    request = uuid.uuid4().hex
    txn = base / request
    txn.mkdir()
    try:
        for entry in m['files']:
            src = safe_file(Path(payload), entry['path'])
            dst = txn / 'payload' / Path(*PurePosixPath(entry['path']).parts)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            if hashlib.sha256(dst.read_bytes()).hexdigest() != entry['sha256']:
                raise ValueError('Copy verification failed')
        if provider(sd) != expected_volume:
            raise ValueError('SD volume changed')
        (txn / 'manifest.json').write_text(json.dumps(m, indent=2), encoding='utf-8')
        result = {'request_id': request, 'state': 'PAYLOAD_STAGED', 'camera_install_requested': False, 'volume': expected_volume}
        (txn / 'status.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        return result
    except Exception as exc:
        (txn / 'status.json').write_text(json.dumps({'request_id': request, 'state': 'ERROR', 'camera_install_requested': False, 'error': str(exc)}), encoding='utf-8')
        raise


def status(sd, request):
    if len(request) != 32 or any(c not in '0123456789abcdef' for c in request):
        raise ValueError('Invalid request ID')
    root = Path(sd) / '.nxwa-extensions'
    if root.is_symlink() or (root / request).is_symlink():
        raise ValueError('Symlink transaction')
    result = json.loads(safe_file(root / request, 'status.json').read_text(encoding='utf-8'))
    if result.get('request_id') != request:
        raise ValueError('Status request ID mismatch')
    return result


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command', required=True)
    for cmd in ('plan', 'check', 'stage'):
        p = sub.add_parser(cmd)
        p.add_argument('--manifest', required=True)
        p.add_argument('--payload', required=True)
        if cmd == 'stage':
            p.add_argument('--sd', required=True)
            p.add_argument('--volume', required=True)
    p = sub.add_parser('status')
    p.add_argument('--sd', required=True)
    p.add_argument('--request-id', required=True)
    args = parser.parse_args()
    try:
        if args.command == 'status':
            result = status(args.sd, args.request_id)
        elif args.command == 'stage':
            result = stage(args.manifest, args.payload, args.sd, args.volume)
        else:
            m = inspect(args.manifest, args.payload)
            result = {'state': 'CHECKED', 'profile': m['profile'], 'camera_install_requested': False}
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
