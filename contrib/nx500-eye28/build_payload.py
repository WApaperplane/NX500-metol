"""Build a strictly allowlisted, disabled offline review archive."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent
UPSTREAM_COMMIT = 'eafc274e0ef0b6cb95b5ad958b2f1ac86dfc3562'
LIBRARY_SHA256 = 'e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e'
LICENSE_SHA256 = '30226ff1d6775ea93c4b1dbcc5fe8b846f0e09d27fd03da0b6c599a15b017e55'
# Reviewed only for the production-disabled offline ARM build.
REVIEWED_HELPER_SHA256 = '927155c7381fb112f2ddcb7ac83b68b6d656acafe2bb6b83e55d0d59ceef09aa'
FILES = (
    'out/nxl-eye-af.so', 'out/eye28-verify.arm', 'LICENSE', 'README.md', 'OFFLINE.md',
    'build.py', 'build_payload.py', 'build_runtime.py', 'SAFETY_REVIEW.md', 'verification.txt', 'src/eye-af.c', 'src/eye_policy.h', 'src/exports.map',
    'tests/eye_replay.c', 'tests/dropout-replay.inc', 'tests/replay.py', 'tests/scenario-sha256.json',
    'metol/selector.sh', 'metol/resume.sh', 'metol/workflow.py',
    'runtime/verify.c', 'runtime/file_identity.c', 'runtime/file_identity.h', 'runtime/proc_identity.c', 'runtime/proc_identity.h', 'runtime/proof.c', 'runtime/proof.h', 'runtime/token.c', 'runtime/token.h', 'runtime/launch.sh', 'runtime/resume.sh', 'runtime/profile.json',
)

def read_safe(root, name):
    rel = PurePosixPath(name)
    if rel.is_absolute() or not rel.parts or any(part in ('', '.', '..') for part in rel.parts):
        raise ValueError('unsafe path: ' + name)
    root = Path(root)
    if root.is_symlink(): raise ValueError('symlink root')
    path = root
    for part in rel.parts:
        path = path / part
        if path.is_symlink(): raise ValueError('symlink: ' + name)
    if not path.is_file(): raise ValueError('missing file: ' + name)
    # O_NOFOLLOW closes final-component symlink substitution on supporting hosts.
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(fd, 'rb') as f:
        if not stat.S_ISREG(os.fstat(f.fileno()).st_mode): raise ValueError('not regular: ' + name)
        return f.read()

def collect(root=ROOT):
    # Check source and license before testing for the optional, unreleased helper.
    library = read_safe(root, 'out/nxl-eye-af.so')
    if hashlib.sha256(library).hexdigest() != LIBRARY_SHA256: raise ValueError('EYE28 library hash mismatch')
    license_data = read_safe(root, 'LICENSE')
    if hashlib.sha256(license_data).hexdigest() != LICENSE_SHA256: raise ValueError('license hash mismatch')
    files = {name: read_safe(root, name) for name in FILES if name != 'out/eye28-verify.arm'}
    if REVIEWED_HELPER_SHA256 is None: raise ValueError('production ARM helper has not been reviewed and pinned')
    helper = read_safe(root, 'out/eye28-verify.arm')
    if hashlib.sha256(helper).hexdigest() != REVIEWED_HELPER_SHA256: raise ValueError('ARM helper hash mismatch')
    files['out/eye28-verify.arm'] = helper
    return files

def main():
    files = collect()
    manifest = {'version':'eye28-metol-offline-1', 'deployment_allowed':False,
                'upstream_commit':UPSTREAM_COMMIT, 'upstream_library_sha256':LIBRARY_SHA256,
                'files':{name:hashlib.sha256(data).hexdigest() for name,data in sorted(files.items())}}
    out = ROOT/'out/eye28-metol-offline-1.zip'
    fd, temp = tempfile.mkstemp(prefix='.eye28-', suffix='.zip', dir=out.parent)
    try:
        with os.fdopen(fd, 'w+b') as stream:
            with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                for name,data in sorted(files.items()): z.writestr(name,data)
                z.writestr('manifest.json', json.dumps(manifest, indent=2, sort_keys=True)+'\n')
        os.replace(temp, out)
    finally:
        if os.path.exists(temp): os.unlink(temp)
    print(out)

if __name__ == '__main__': main()
