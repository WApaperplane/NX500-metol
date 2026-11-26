#!/bin/sh
# Withdraw only unarmed guards. Does not restore or write the snapshot image.
# Tokens are filled by build_eye_capture.py so a rebuilt guard never pairs with
# a stale hard-coded hash.
set -eu
state=/opt/usr/nx-eye-af
rt=/usr/local/lib/nx-eye-af
backup=$state/capture-writers-before
bb=/opt/usr/nx-ks/busybox
drop=/etc/systemd/system/hibernation.service.d
fail() { echo "RESULT=$1" >&2; exit 1; }
hash() { "$bb" sha256sum "$1" | cut -d' ' -f1; }
[ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail CAPTURE_STATE_REQUIRES_REVIEW
[ -d "$backup" ] && [ ! -L "$backup" ] || fail BACKUP_MISSING
[ "$(hash "$backup/hibernation.sh")" = dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361 ] || fail BACKUP_CHANGED
[ "$(hash "$backup/make_snapshot.sh")" = 08df6e37016b022944f4d928edf6bb95477327d705001d1af75ad367d198b8c1 ] || fail BACKUP_CHANGED
writer=
case "$(hash /usr/bin/hibernation.sh)" in
 @W_HEADER@) writer=r4;;
 @W_HEADER_LEGACY@) writer=r2;;
 *) fail WRITER_CHANGED;;
esac
[ "$(hash /usr/bin/make_snapshot.sh)" = @W_FACTORY@ ] || fail FACTORY_WRITER_CHANGED
if [ -e "$rt/verify-before-capture.sh" ]; then
 [ "$(hash "$rt/verify-before-capture.sh")" = @W_GUARD@ ] || fail GUARD_UNKNOWN
fi
mount -o remount,rw /
trap 'mount -o remount,ro / || :' 0
cp "$backup/hibernation.sh" /usr/bin/hibernation.sh.nx-eye-withdraw
chmod 755 /usr/bin/hibernation.sh.nx-eye-withdraw
mv /usr/bin/hibernation.sh.nx-eye-withdraw /usr/bin/hibernation.sh
cp "$backup/make_snapshot.sh" /usr/bin/make_snapshot.sh.nx-eye-withdraw
chmod 755 /usr/bin/make_snapshot.sh.nx-eye-withdraw
mv /usr/bin/make_snapshot.sh.nx-eye-withdraw /usr/bin/make_snapshot.sh
[ ! -f "$drop/75-nx-eye-prewrite.conf" ] || mv "$drop/75-nx-eye-prewrite.conf" "$backup/prewrite.withdrawn-$writer"
rmdir "$drop" 2>/dev/null || :
[ ! -f "$rt/verify-before-capture.sh" ] || mv "$rt/verify-before-capture.sh" "$backup/guard.withdrawn-$writer"
[ ! -f "$rt/arm-rescue.sh" ] || mv "$rt/arm-rescue.sh" "$backup/rescue.withdrawn-$writer"
[ ! -f "$rt/battery-gate.sh" ] || mv "$rt/battery-gate.sh" "$backup/battery-gate.withdrawn-$writer"
systemctl daemon-reload
printf 'writer_withdrawn=%s\nphase=GUARDS_WITHDRAWN\n' "$writer" >> "$backup/status"
sync
echo "RESULT=GUARDS_WITHDRAWN_SNAPSHOT_UNCHANGED"
echo "WRITER_WITHDRAWN=$writer"
