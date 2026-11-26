#!/bin/sh
# Withdraw only unarmed guards. Does not restore or write the snapshot image.
set -eu
state=/opt/usr/nx-eye-af
rt=/usr/local/lib/nx-eye-af
backup=$state/capture-writers-before
bb=/opt/usr/nx-ks/busybox
pin() { actual=$("$bb" sha256sum "$1") && [ "${actual%% *}" = "$2" ]; }
fail() { echo "RESULT=$1" >&2; exit 1; }
[ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail CAPTURE_STATE_REQUIRES_REVIEW
[ -d "$backup" ] && [ ! -L "$backup" ] || fail BACKUP_MISSING
pin "$backup/hibernation.sh" dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361 || fail BACKUP_CHANGED
pin "$backup/make_snapshot.sh" 08df6e37016b022944f4d928edf6bb95477327d705001d1af75ad367d198b8c1 || fail BACKUP_CHANGED
pin /usr/bin/hibernation.sh 1e2bcf8383c0193c9bbe5f21b882d8bef14a765897bf50fc01c5b49447296ab2 || fail WRITER_CHANGED
pin /usr/bin/make_snapshot.sh ea12c6cf1e5d4fa7ac1732ab6711992e0a603dfbe7e015dbcebc7db6217bee66 || fail WRITER_CHANGED
mount -o remount,rw /
trap 'mount -o remount,ro /' 0
cp "$backup/hibernation.sh" /usr/bin/hibernation.sh.nx-eye-withdraw
chmod 755 /usr/bin/hibernation.sh.nx-eye-withdraw
mv /usr/bin/hibernation.sh.nx-eye-withdraw /usr/bin/hibernation.sh
cp "$backup/make_snapshot.sh" /usr/bin/make_snapshot.sh.nx-eye-withdraw
chmod 755 /usr/bin/make_snapshot.sh.nx-eye-withdraw
mv /usr/bin/make_snapshot.sh.nx-eye-withdraw /usr/bin/make_snapshot.sh
mv /etc/systemd/system/hibernation.service.d/75-nx-eye-prewrite.conf "$backup/prewrite.withdrawn"
rmdir /etc/systemd/system/hibernation.service.d 2>/dev/null || :
mv "$rt/verify-before-capture.sh" "$backup/guard.withdrawn"
systemctl daemon-reload
printf 'phase=GUARDS_WITHDRAWN\n' >> "$backup/status"
sync
echo RESULT=GUARDS_WITHDRAWN_SNAPSHOT_UNCHANGED
