#!/bin/sh
# Explicit operator-ready boundary. No direct snapshot erase or application restart.
# @GUARD@/@HEADER@/@FACTORY@ are filled by build_eye_capture.py from the exact
# files in the same guards package; never hand-edit those hashes.
set -eu
umask 077
state=/opt/usr/nx-eye-af
rt=/usr/local/lib/nx-eye-af
bb=/opt/usr/nx-ks/busybox
fail() { echo "RESULT=$1" >&2; exit 1; }
pin() { actual=$("$bb" sha256sum "$1") && [ "${actual%% *}" = "$2" ]; }
case "${1:-}" in --operator-ready|--check-ready) ;; *) fail OPERATOR_READINESS_REQUIRED;; esac
[ -d "$state" ] && [ ! -L "$state" ] || fail NOT_PREPARED
[ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] && [ ! -e "$state/launch-claim" ] || fail ALREADY_ARMED
grep -q '^phase=GUARDS_INSTALLED_NOT_ARMED$' "$state/capture-writers-before/status" || fail GUARDS_NOT_INSTALLED
/bin/sh "$rt/verify-payload.sh" "$rt" || fail PAYLOAD_CHANGED
[ -x "$rt/verify-before-capture.sh" ] || fail GUARD_MISSING
pin "$rt/verify-before-capture.sh" @GUARD@ || fail CAPTURE_GUARD_CHANGED
pin /usr/bin/hibernation.sh @HEADER@ || fail HEADER_GUARD_CHANGED
pin /usr/bin/make_snapshot.sh @FACTORY@ || fail ERASE_GUARD_CHANGED
[ "$(cat /sys/class/extcon/USB/state)" = 0 ] || fail USB_CONNECTED
[ -f /tmp/.hib_post ] && [ ! -e /opt/etc/.hib_capturing ] || fail NOT_NORMAL_MODE
awk 'NR==1{ok=($1=="Filename")} $1=="/dev/mmcblk0p9"{busy=1} END{exit !(ok&&!busy)}' /proc/swaps || fail SNAPSHOT_BUSY
[ "$(cat /sys/class/block/mmcblk0p9/size)" = 307199 ] || fail PARTITION_CHANGED
current=$($bb md5sum /dev/mmcblk0p9)
[ "${current%% *}" = 8a669a6a1b503d541a7f4a463d49ca5e ] || fail BACKED_UP_IMAGE_CHANGED
# An ordinary startup may restore the old manager from the existing snapshot.
# Reload disk units without restarting the running camera before inspecting it.
systemctl daemon-reload
systemctl --all show hib_post.service -p Wants | tr '= ' '\n\n' | grep -Fqx nx-eye-resume.service || fail WAKE_DEPENDENCY_MISSING
systemctl show di-camera-app.service -p ExecStart | grep -Fq '/bin/sh /usr/local/lib/nx-eye-af/launch.sh' || fail WRONG_LAUNCHER
pid=$(systemctl show di-camera-app.service -p MainPID | sed -n 's/^MainPID=//p')
case "$pid" in ''|0|*[!0-9]*) fail INVALID_CAMERA_PID;; esac
if ls -l "/proc/$pid/fd" | grep -E 'MP4|mp4' >/dev/null; then fail CLOSE_MEDIA_BEFORE_CAPTURE; fi
"$bb" cmp -s /dev/null /dev/null || fail COMPARE_TOOL_UNAVAILABLE
if [ "$1" = --check-ready ]; then echo RESULT=READY_NOT_ARMED; exit 0; fi
case " $* " in *" --check-ready "*) echo RESULT=READY_NOT_ARMED; exit 0;; esac
# Battery evidence. See battery-gate.sh: a raw value such as 1460 is not a
# percentage and needs the operator's explicit --battery-confirmed-full claim.
case " $* " in *" --battery-confirmed-full "*) battery_confirmed=1;; *) battery_confirmed=0;; esac
if ! /bin/sh "$rt/battery-gate.sh" /sys/class/power_supply/battery "$battery_confirmed" > "$state/battery.txt" 2> "$state/battery.reason"; then
 fail "$(cat "$state/battery.reason" 2>/dev/null || echo BATTERY_CHECK_FAILED)"
fi
boot=$(cat /proc/sys/kernel/random/boot_id)
[ -n "$boot" ] || fail INVALID_BOOT_ID
tag=$(date +%Y%m%d%H%M%S)-$$
# Operator must already have set English/M, body cap and full battery.
(set -C; printf '%s\nEYE\n%s\n' "$boot" "$tag" > "$state/armed")
if ! cp "$state/armed" "$state/capture-origin"; then
 mv "$state/armed" "$state/armed-copy-failed-$tag"
 fail GENERATION_COPY_FAILED
fi
if ! "$bb" cmp -s "$state/armed" "$state/capture-origin"; then
 mv "$state/armed" "$state/generation-rejected-$tag"
 mv "$state/capture-origin" "$state/origin-rejected-$tag"
 fail GENERATION_MISMATCH
fi
printf 'phase=RESCUE_REQUESTED\nboot_id=%s\nrequest=%s\n' "$boot" "$tag" > "$state/capture-request.txt"
sync
cd /
# Rescue must survive loss of the network session. Caller dispatches this root-
# backed script detached, with stdin /dev/null and console output.
if /usr/bin/systemctl rescue; then
 echo rescue_request_return=0 >> "$state/capture-request.txt"
else
 rc=$?
 printf 'rescue_request_return=%s\n' "$rc" >> "$state/capture-request.txt"
 if [ ! -e "$state/launch-claim" ] && [ ! -e /opt/etc/.hib_capturing ] &&
    awk 'NR==1{ok=($1=="Filename")} $1=="/dev/mmcblk0p9"{busy=1} END{exit !(ok&&!busy)}' /proc/swaps; then
  # A rejected rescue must not leave a half generation behind: the next boot
  # would find armed+capture-origin but no claim, and a retry would be blocked.
  mv "$state/armed" "$state/rescue-rejected-$tag"
  [ ! -e "$state/capture-origin" ] && [ ! -L "$state/capture-origin" ] ||
   mv "$state/capture-origin" "$state/origin-rejected-$tag"
 fi
 exit "$rc"
fi
