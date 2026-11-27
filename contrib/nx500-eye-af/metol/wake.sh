#!/bin/sh
# Resume gate only: no application restart, injection, SD access or key listener.
set -u
umask 077
root=
if [ "${1:-}" = --test-root ]; then root=${2:?}; fi
state=$root/opt/usr/nx-eye-af
run=$root/tmp/nx-lut-lite
lib=
for cand in nxl-eye-af.so nxl-eye-af-yunet.so; do
 [ -f "/usr/local/lib/nx-eye-af/$cand" ] || continue
 lib=/usr/local/lib/nx-eye-af/$cand; break
done
fail() { echo "$1" >&2; exit 1; }
regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
normal() {
 [ "$(cat "$root/sys/class/extcon/USB/state" 2>/dev/null)" = 0 ] &&
 [ -f "$root/tmp/.hib_post" ] && [ ! -e "$root/opt/etc/.hib_capturing" ] || return 1
 case "$(cat "$root/sys/power/bootmode" 2>/dev/null)" in 2|3) ;; *) return 1;; esac
 awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$root/proc/swaps"
}
if [ -z "$root" ]; then /bin/sh /usr/local/lib/nx-eye-af/verify-payload.sh || fail PAYLOAD_UNVERIFIED; fi
normal || fail NOT_NORMAL_RESUME
[ -n "$lib" ] || fail LIBRARY_MISSING
[ -d "$state" ] && [ ! -L "$state" ] || fail STATE_MISSING
for f in armed capture-origin launch-claim; do regular "$state/$f" || fail PROOF_MISSING; done
if command -v cmp >/dev/null 2>&1; then
 cmp -s "$state/armed" "$state/capture-origin" || fail GENERATION_MISMATCH
else
 /opt/usr/nx-ks/busybox cmp -s "$state/armed" "$state/capture-origin" || fail GENERATION_MISMATCH
fi
{ IFS= read -r previous && IFS= read -r policy; } < "$state/armed" || fail INVALID_ARMED
current=$(cat "$root/proc/sys/kernel/random/boot_id") || fail BOOT_ID_MISSING
[ -n "$previous" ] && [ -n "$current" ] && [ "$previous" != "$current" ] && [ "$policy" = EYE ] || fail GENERATION_MISMATCH
claim=$state/launch-claim
for key in boot_id policy camera_pid file mapped_dev mapped_ino; do
 [ "$(grep -c "^$key=" "$claim")" = 1 ] || fail INVALID_CLAIM
done
grep -Fqx "boot_id=$current" "$claim" && grep -Fqx 'policy=EYE' "$claim" && grep -Fqx "file=$lib" "$claim" || fail CLAIM_MISMATCH
if [ -n "$root" ]; then resume=$(cat "$root/test-dmesg" 2>/dev/null); else resume=$(dmesg); fi
printf '%s\n' "$resume" | grep -Fq 'kernel_resume start!' || fail RESUME_UNCONFIRMED
pid=$(sed -n 's/^camera_pid=//p' "$claim")
case "$pid" in ''|0|*[!0-9]*) fail INVALID_PID;; esac
[ "$(readlink "$root/proc/$pid/exe" 2>/dev/null)" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ] || fail CAMERA_MISMATCH
dev=$(sed -n 's/^mapped_dev=//p' "$claim"); ino=$(sed -n 's/^mapped_ino=//p' "$claim")
[ -n "$dev" ] && [ -n "$ino" ] || fail INVALID_MAPPING
awk -v d="$dev" -v i="$ino" -v p="$lib" '$4==d && $5==i && $2~/x/ && $6==p && NF==6 {ok=1} END {exit !ok}' "$root/proc/$pid/maps" || fail LIBRARY_MISMATCH
[ ! -e "$state/disabled" ] || fail DISABLED
[ ! -L "$run" ] && mkdir -p "$run" && mkdir "$run/metol-eye-wake.lock" || fail LOCKED
trap 'rmdir "$run/metol-eye-wake.lock"' 0
token=$run/eye-af.resume-ready
if [ -e "$token" ] || [ -L "$token" ]; then
 regular "$token" && [ "$(cat "$token")" = "$pid" ] || fail TOKEN_CONFLICT
else
 (set -C; printf '%s\n' "$pid" > "$token") || fail TOKEN_FAILED
fi
echo EYE_READY
