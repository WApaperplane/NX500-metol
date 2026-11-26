#!/usr/bin/env python3
"""Create an offline Metol Eye payload with a pinned verifier; never install it."""
import argparse, hashlib, json, pathlib, shutil
HERE = pathlib.Path(__file__).resolve().parent
FILES = {'nxl-eye-af-yunet.so': HERE/'out/nxl-eye-af-yunet.so', 'claim.arm': HERE/'out/claim.arm', 'launch.sh': HERE/'metol/launch.sh', 'wake.sh': HERE/'metol/wake.sh', 'nx-eye-resume.service': HERE/'metol/nx-eye-resume.service', 'di-camera-app.conf.example': HERE/'metol/di-camera-app.conf.example', 'YUNET-LICENSE.txt': HERE/'model/YUNET-LICENSE.txt', 'YUNET.md': HERE/'model/YUNET.md', 'LICENSE': HERE/'LICENSE'}
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__); p.add_argument('--output',type=pathlib.Path,required=True); a=p.parse_args(); out=a.output.resolve()
 if out.exists(): raise SystemExit('Output already exists; choose a new directory')
 missing=[str(p) for p in FILES.values() if not p.is_file()]
 if missing: raise SystemExit('Missing inputs: '+', '.join(missing))
 out.mkdir(parents=True)
 for name, source in FILES.items(): shutil.copyfile(source,out/name)
 hashes={name:digest(out/name) for name in FILES}
 checks=['#!/bin/sh','set -eu','base=${1:-/usr/local/lib/nx-eye-af}', 'test -d "$base" && test ! -L "$base" || exit 1', 'hash_file() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else /opt/usr/nx-ks/busybox sha256sum "$1"; fi; }']
 for name, h in hashes.items():
  checks += [f'test -f "$base/{name}" && test ! -L "$base/{name}" || exit 1',f'actual=$(hash_file "$base/{name}") || exit 1',f'[ "${{actual%% *}}" = {h} ] || exit 1']
 (out/'verify-payload.sh').write_text('\n'.join(checks)+'\n',encoding='utf-8')
 manifest={'schema':1,'upstream':'WApaperplane/NX500-metol@4a64b061d41da0701f303646cea096daf61c0b93','sha256':hashes,'verifier_sha256':digest(out/'verify-payload.sh')}
 manifest.update(schema_version=1, profile='eye-yunet32', model='NX500', firmware='1.12',
                 target_root='/usr/local/lib/nx-eye-af', camera_install_requested=False)
 manifest['files']=[{'path':name,'sha256':h,'mode':'0755' if name.endswith(('.sh','.arm')) else '0644'} for name,h in hashes.items()]
 manifest['files'].append({'path':'verify-payload.sh','sha256':manifest['verifier_sha256'],'mode':'0755'})
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8')
 print(json.dumps(manifest,indent=2,sort_keys=True))
if __name__=='__main__': main()
