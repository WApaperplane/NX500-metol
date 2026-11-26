"""Build a pinned, prepare-only Eye installer. Never arms or requests capture."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

UNIT_SHA = "ac1bab448ef00c8038104742861d631dc3991d42ef9be575779af72ac778620d"
T2_SHA = "99931046311096f82ff58f3d6cdf951baf1053427909d2de582364d9972139c7"
APP_SHA = "ec3951233eb81af69f4461cc2e8c44d247ec03f4467305cd8200b11f7f61bca4"

SCRIPT = r'''#!/bin/sh
# Prepare only: no armed record, rescue, application restart or image write.
set -eu
umask 077
pkg=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
rt=/usr/local/lib/nx-eye-af
state=/opt/usr/nx-eye-af
drop=/etc/systemd/system/di-camera-app.service.d
unit=/etc/systemd/system/nx-eye-resume.service
wants=/etc/systemd/system/hib_post.service.wants
link=$wants/nx-eye-resume.service
bb=/opt/usr/nx-ks/busybox
hash() { "$bb" sha256sum "$1" | cut -d' ' -f1; }
fail() { echo "RESULT=$1" >&2; exit 1; }
pin() { [ -f "$1" ] && [ ! -L "$1" ] && [ "$(hash "$1")" = "$2" ]; }
check() {
 grep -q '^NX500$' /etc/version.info && grep -q '^1.12$' /etc/version.info || fail FIRMWARE
 pin /usr/lib/systemd/system/di-camera-app.service @UNIT@ || fail SERVICE_CHANGED
 pin /opt/usr/nx-ks/dicam/t2_premount.sh @T2@ || fail T2_SCRIPT_CHANGED
 pin /opt/usr/nx-ks/dicam/di-camera-app.c2 @APP@ || fail T2_APP_CHANGED
 pin "$pkg/payload/verify-payload.sh" @VERIFY@ || fail UNTRUSTED_VERIFIER
 /bin/sh "$pkg/payload/verify-payload.sh" "$pkg/payload" || fail PAYLOAD_MISMATCH
 [ "$(cat /sys/class/extcon/USB/state)" = 0 ] || fail USB_CONNECTED
 awk 'NR==1{ok=($1=="Filename")} $1=="/dev/mmcblk0p9"{busy=1} END{exit !(ok&&!busy)}' /proc/swaps || fail SNAPSHOT_BUSY
 for path in "$rt" "$state" "$drop" "$unit" "$link"; do
  [ ! -e "$path" ] && [ ! -L "$path" ] || fail EXISTING_EYE_OR_OVERRIDE
 done
 [ ! -L "$wants" ] || fail FOREIGN_WANTS
}
rollback() {
 backup=${2:-}
 case "$backup" in /opt/usr/nx-eye-prepare-*) ;; *) fail INVALID_BACKUP;; esac
 [ -d "$backup" ] && [ ! -L "$backup" ] || fail INVALID_BACKUP
 [ ! -e "$rt/verify-before-capture.sh" ] || fail WITHDRAW_CAPTURE_GUARDS_FIRST
 grep -q '^phase=PREPARED_NOT_ARMED$' "$backup/status" || fail NOT_PREPARED
 [ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail CAPTURE_ALREADY_ARMED
 pin "$rt/verify-payload.sh" @VERIFY@ || fail INSTALLED_PAYLOAD_CHANGED
 /bin/sh "$rt/verify-payload.sh" "$rt" || fail INSTALLED_PAYLOAD_CHANGED
 "$bb" cmp -s "$drop/70-metol-eye.conf" "$rt/di-camera-app.conf.example" || fail DROPIN_CHANGED
 "$bb" cmp -s "$unit" "$rt/nx-eye-resume.service" || fail UNIT_CHANGED
 [ "$(readlink "$link")" = "$unit" ] || fail LINK_CHANGED
 mount -o remount,rw /
 trap 'mount -o remount,ro /' 0
 mv "$link" "$backup/link.withdrawn"
 mv "$unit" "$backup/unit.withdrawn"
 # Only remove this installer's drop-in. Preserve any later foreign drop-ins.
 mv "$drop/70-metol-eye.conf" "$backup/dropin.withdrawn"
 rmdir "$drop" 2>/dev/null || :
 mv "$rt" "$backup/runtime.withdrawn"
 mv "$state" "$backup/state.withdrawn"
 systemctl daemon-reload
 printf 'phase=WITHDRAWN\n' >> "$backup/status"
 sync
 echo RESULT=WITHDRAWN_WITHOUT_SNAPSHOT_WRITE
}
case "${1:-check}" in
 check) check; echo RESULT=PREPARE_CHECKED; exit 0;;
 install) check;;
 rollback) rollback "$@"; exit 0;;
 *) fail USAGE_CHECK_INSTALL_OR_ROLLBACK;;
esac
# Runtime is intentionally installed on rootfs, not a bind mount on p14.
tag=$(date +%Y%m%d%H%M%S)-$$
backup=/opt/usr/nx-eye-prepare-$tag
mkdir "$backup"
cp /usr/lib/systemd/system/di-camera-app.service "$backup/camera.service.before"
boot=$(cat /proc/sys/kernel/random/boot_id)
printf 'boot_id=%s\nphase=PREPARING\n' "$boot" > "$backup/status"
committed=0
rw=0
owned_rt=0; owned_state=0; owned_drop=0; owned_unit=0; owned_link=0
finish() {
 rc=$?
 if [ "$committed" = 0 ]; then
  [ "$owned_link" = 0 ] || mv "$link" "$backup/link.failed"
  [ "$owned_unit" = 0 ] || mv "$unit" "$backup/unit.failed"
  [ "$owned_drop" = 0 ] || mv "$drop" "$backup/drop.failed"
  [ "$owned_rt" = 0 ] || mv "$rt" "$backup/runtime.failed"
  [ "$owned_state" = 0 ] || mv "$state" "$backup/state.failed"
  systemctl daemon-reload || :
  printf 'phase=ROLLED_BACK\nexit_code=%s\n' "$rc" >> "$backup/status"
 fi
 [ "$rw" = 0 ] || mount -o remount,ro / || rc=1
 exit "$rc"
}
trap finish 0
mount -o remount,rw /; rw=1
mkdir -p /usr/local/lib
mkdir "$rt"; owned_rt=1
cp "$pkg/payload/"* "$rt/"
chmod 755 "$rt/"*.sh "$rt/claim.arm"
chmod 644 "$rt/nxl-eye-af-yunet.so"
pin "$rt/verify-payload.sh" @VERIFY@ || fail INSTALLED_VERIFIER
/bin/sh "$rt/verify-payload.sh" "$rt" || fail INSTALLED_PAYLOAD
mkdir "$state"; owned_state=1
mkdir -p /etc/systemd/system
mkdir "$drop"; owned_drop=1
cp "$rt/di-camera-app.conf.example" "$drop/70-metol-eye.conf"
cp "$rt/nx-eye-resume.service" "$unit"; owned_unit=1
mkdir -p "$wants"
ln -s "$unit" "$link"; owned_link=1
systemctl daemon-reload
systemctl show di-camera-app.service -p ExecStart > "$backup/exec-after.txt"
grep -Fq '/bin/sh /usr/local/lib/nx-eye-af/launch.sh' "$backup/exec-after.txt" || fail EXEC_NOT_SELECTED
systemctl --all show hib_post.service -p Wants > "$backup/wants-after.txt"
tr '= ' '\n\n' < "$backup/wants-after.txt" | grep -Fqx nx-eye-resume.service || fail WANTS_NOT_LOADED
[ ! -e "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail UNEXPECTED_CAPTURE_RECORD
printf 'phase=PREPARED_NOT_ARMED\nbackup=%s\n' "$backup" >> "$backup/status"
sync
committed=1
echo RESULT=PREPARED_NOT_ARMED
echo BACKUP=$backup
'''


def build(payload, output):
    payload, output = Path(payload), Path(output)
    manifest = json.loads((payload / "manifest.json").read_text())
    if manifest.get("profile") != "eye-yunet32":
        raise ValueError("Wrong profile")
    files = manifest["files"]
    for f in files:
        name = f["path"]
        if Path(name).name != name or "\\" in name or ":" in name or name in (".", ".."):
            raise ValueError("Unsafe member")
        path = payload / name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != f["sha256"]:
            raise ValueError("Payload changed")
    verifier = hashlib.sha256((payload / "verify-payload.sh").read_bytes()).hexdigest()
    if verifier != manifest["verifier_sha256"]:
        raise ValueError("Verifier changed")
    script = SCRIPT.replace("@UNIT@", UNIT_SHA).replace("@T2@", T2_SHA).replace("@APP@", APP_SHA).replace("@VERIFY@", verifier)
    if output.exists():
        raise ValueError("Output must be new")
    output.mkdir(parents=True)
    (output / "prepare.sh").write_text(script)
    with tarfile.open(output / "prepare.tar", "x") as t:
        t.add(output / "prepare.sh", arcname="prepare.sh")
        for f in files:
            t.add(payload / f["path"], arcname="payload/" + f["path"])
    return output / "prepare.tar"


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("payload", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    print(build(a.payload, a.output))
