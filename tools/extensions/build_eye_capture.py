"""Build protected OEM writer sources from this camera's verified archive.

The output is preparation material. It never writes a device or invokes rescue.
"""
import argparse
import hashlib
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = "/bin/sh /usr/local/lib/nx-eye-af/verify-before-capture.sh"
# Hand the boot back to the stock camera. Without this marker the camera app
# waits forever and the screen stays black.
COLD_READY = "\t/bin/touch /tmp/.hib_ready\n\texit 0\n"
ZRAM_OFF = """\
\tif command -v swapoff >/dev/null 2>&1; then
\t\tswapoff /dev/zram0 || :
\telif [ -x /sbin/swapoff ]; then
\t\t/sbin/swapoff /dev/zram0 || :
\telif [ -x /bin/busybox ]; then
\t\t/bin/busybox swapoff /dev/zram0 || :
\tfi
"""
INSTALL = r'''#!/bin/sh
# Install write guards only; no arm or rescue operation.
set -eu
umask 077
src=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
rt=/usr/local/lib/nx-eye-af
state=/opt/usr/nx-eye-af
bb=/opt/usr/nx-ks/busybox
hash() { "$bb" sha256sum "$1" | cut -d' ' -f1; }
fail() { echo "RESULT=$1" >&2; exit 1; }
[ "$(hash "$src/capture.sha256")" = @TABLE@ ] || fail UNTRUSTED_GUARDS
(cd "$src" && "$bb" sha256sum -c capture.sha256) || fail CORRUPT_GUARDS
[ -d "$state" ] && [ ! -L "$state" ] || fail EYE_NOT_PREPARED
[ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail ALREADY_ARMED
/bin/sh "$rt/verify-payload.sh" "$rt" || fail EYE_PAYLOAD_CHANGED
[ "$(hash /usr/bin/hibernation.sh)" = @HEADER@ ] || fail FOREIGN_HEADER_WRITER
[ "$(hash /usr/bin/make_snapshot.sh)" = @FACTORY@ ] || fail FOREIGN_FACTORY_WRITER
[ ! -e "$rt/verify-before-capture.sh" ] && [ ! -L "$rt/verify-before-capture.sh" ] || fail GUARD_EXISTS
[ ! -e "$rt/arm-rescue.sh" ] && [ ! -L "$rt/arm-rescue.sh" ] || fail RESCUE_EXISTS
drop=/etc/systemd/system/hibernation.service.d
[ ! -e "$drop" ] && [ ! -L "$drop" ] || fail FOREIGN_CAPTURE_OVERRIDE
backup=$state/capture-writers-before
mkdir "$backup" || fail BACKUP_EXISTS
cp /usr/bin/hibernation.sh "$backup/hibernation.sh"
cp /usr/bin/make_snapshot.sh "$backup/make_snapshot.sh"
[ "$(hash "$backup/hibernation.sh")" = @HEADER@ ] && [ "$(hash "$backup/make_snapshot.sh")" = @FACTORY@ ] || fail BACKUP_MISMATCH
committed=0; rw=0; wrote_header=0; wrote_factory=0; wrote_guard=0; wrote_rescue=0; wrote_drop=0
finish() {
 rc=$?
 if [ "$committed" = 0 ]; then
  [ "$wrote_header" = 0 ] || cp "$backup/hibernation.sh" /usr/bin/hibernation.sh
  [ "$wrote_factory" = 0 ] || cp "$backup/make_snapshot.sh" /usr/bin/make_snapshot.sh
  [ "$wrote_guard" = 0 ] || mv "$rt/verify-before-capture.sh" "$backup/guard.failed"
  [ "$wrote_rescue" = 0 ] || mv "$rt/arm-rescue.sh" "$backup/rescue.failed"
  [ "$wrote_drop" = 0 ] || mv "$drop" "$backup/drop.failed"
  systemctl daemon-reload || :
 fi
 [ "$rw" = 0 ] || mount -o remount,ro / || rc=1
 exit "$rc"
}
trap finish 0
mount -o remount,rw /; rw=1
cp "$src/verify-before-capture.sh" "$rt/verify-before-capture.sh"; wrote_guard=1
chmod 755 "$rt/verify-before-capture.sh"
cp "$src/arm-rescue.sh" "$rt/arm-rescue.sh"; wrote_rescue=1
chmod 755 "$rt/arm-rescue.sh"
cp "$src/battery-gate.sh" "$rt/battery-gate.sh"
chmod 755 "$rt/battery-gate.sh"
cp "$src/hibernation.sh" /usr/bin/hibernation.sh.nx-eye-new
chmod 755 /usr/bin/hibernation.sh.nx-eye-new
mv /usr/bin/hibernation.sh.nx-eye-new /usr/bin/hibernation.sh; wrote_header=1
cp "$src/make_snapshot.sh" /usr/bin/make_snapshot.sh.nx-eye-new
chmod 755 /usr/bin/make_snapshot.sh.nx-eye-new
mv /usr/bin/make_snapshot.sh.nx-eye-new /usr/bin/make_snapshot.sh; wrote_factory=1
mkdir "$drop"; wrote_drop=1
cp "$src/capture-profile.conf" "$drop/75-nx-eye-prewrite.conf"
systemctl daemon-reload
systemctl --all show hib_post.service -p Wants | tr '= ' '\n\n' | grep -Fqx nx-eye-resume.service || fail WANTS_NOT_LOADED
printf 'phase=GUARDS_INSTALLED_NOT_ARMED\n' > "$backup/status"
sync
committed=1
echo RESULT=GUARDS_INSTALLED_NOT_ARMED
'''
PINS = {
    "usr/bin/hibernation.sh": "dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361",
    "usr/bin/make_snapshot.sh": "08df6e37016b022944f4d928edf6bb95477327d705001d1af75ad367d198b8c1",
}
LED = b"echo timer > /sys/class/leds/card/trigger\n"
ANNOUNCE = b'echo "Starting hibernation ... "\n'
IMAGE_WRITE = b"/bin/dd if=/dev/zero of=$SNAPSHOT_DEV bs=512 count=1\n"


def replace_once(data, anchor, replacement):
    if data.count(anchor) != 1:
        raise ValueError("Ambiguous write boundary: " + anchor.decode("ascii", "replace").strip())
    return data.replace(anchor, replacement)


def protected_header(original):
    """Gate hibernation.sh before zram and before the image header.

    A rejecting gate must leave the image header intact, drop the hibernation
    zram swap and publish /tmp/.hib_ready so the stock camera keeps booting.
    Exiting non-zero here is what strands the camera on a black screen.
    """
    source = original
    # The card LED is an OEM progress signal. It may only light once the write
    # gate has passed, otherwise a rejecting capture still looks like progress.
    source = replace_once(source, LED, b"")
    source = replace_once(source, ANNOUNCE, ANNOUNCE + ("if ! %s --at-preparation; then\n" % CHECK).encode() +
                          COLD_READY.encode() + b"fi\n")
    gate = ("if ! %s --at-image-write; then\n" % CHECK).encode() + ZRAM_OFF.encode() + COLD_READY.encode() + \
        b"fi\n" + LED
    source = replace_once(source, IMAGE_WRITE, gate + IMAGE_WRITE)
    # Structural proof of the intended shape, not just "the bytes changed".
    text = source.decode("ascii")
    if text.count("--at-preparation") != 1 or text.count("--at-image-write") != 1:
        raise ValueError("Gate count changed")
    if text.count("/tmp/.hib_ready") != original.count(b"/tmp/.hib_ready") + 2:
        raise ValueError("Cold-ready handoff missing")
    if text.count(LED.decode()) != 1 or text.index(LED.decode()) < text.index("--at-image-write"):
        raise ValueError("Card LED is not behind the write gate")
    if text.index("--at-preparation") > text.index("mkswap /dev/zram0"):
        raise ValueError("Preparation gate runs after zram")
    if text.index("swapoff") > text.index(IMAGE_WRITE.decode()):
        raise ValueError("zram release is not on the rejecting path")
    return source


def build(backup, output):
    if output.exists():
        raise ValueError("Output must be new")
    guard = (ROOT / "contrib/nx500-eye-af/capture/verify-before-capture.sh").read_bytes()
    rescue = (ROOT / "contrib/nx500-eye-af/capture/arm-rescue.sh").read_text()
    files = {"verify-before-capture.sh": guard,
             "battery-gate.sh": (ROOT / "contrib/nx500-eye-af/capture/battery-gate.sh").read_bytes()}
    with tarfile.open(backup) as archive:
        for name, pin in PINS.items():
            original = archive.extractfile(name).read()
            if hashlib.sha256(original).hexdigest() != pin:
                raise ValueError("Unknown OEM writer: " + name)
            if name.endswith("/hibernation.sh"):
                files[Path(name).name] = protected_header(original)
            else:
                anchor = b"if [ -e /etc/snapshot ]; then\n"
                files[Path(name).name] = replace_once(
                    original, anchor,
                    anchor + b"        echo 'Existing snapshot protected; use reviewed OEM rescue capture' >&2\n        exit 1\n")
    files["capture-profile.conf"] = b"[Service]\nExecStartPre=/bin/sh /usr/local/lib/nx-eye-af/verify-before-capture.sh --when-capturing\n"
    # The rescue boundary pins the installed guard and writers. Emit them here so
    # a rebuilt guard can never be paired with a stale hard-coded hash.
    for token, value in (("@GUARD@", hashlib.sha256(guard).hexdigest()),
                         ("@HEADER@", hashlib.sha256(files["hibernation.sh"]).hexdigest()),
                         ("@FACTORY@", hashlib.sha256(files["make_snapshot.sh"]).hexdigest())):
        if token not in rescue:
            raise ValueError("Missing template token " + token)
        rescue = rescue.replace(token, value)
    files["arm-rescue.sh"] = rescue.encode()
    output.mkdir(parents=True)
    for name, data in files.items():
        (output / name).write_bytes(data)
    table = "".join(hashlib.sha256(b).hexdigest() + "  " + n + "\n" for n, b in sorted(files.items()))
    (output / "capture.sha256").write_text(table)
    script = INSTALL.replace("@TABLE@", hashlib.sha256(table.encode()).hexdigest()).replace("@HEADER@", PINS["usr/bin/hibernation.sh"]).replace("@FACTORY@", PINS["usr/bin/make_snapshot.sh"])
    (output / "install-guards.sh").write_text(script)
    return files


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("backup", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    build(args.backup, args.output)
