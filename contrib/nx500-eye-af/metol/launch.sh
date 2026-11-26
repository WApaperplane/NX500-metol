#!/bin/sh
# Camera service wrapper; safe fallback always execs the stock app.
state=/opt/usr/nx-eye-af
lib=/usr/local/lib/nx-eye-af/nxl-eye-af-yunet.so
app=/usr/apps/com.samsung.di-camera-app/bin/di-camera-app
eligible() {
 # Keep the existing T2 premount. Only these independently audited binaries
 # may receive the preload; unknown applications retain native startup.
 identity=$(/opt/usr/nx-ks/busybox sha256sum "$app") || return 1
 case "${identity%% *}" in
 55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04|ec3951233eb81af69f4461cc2e8c44d247ec03f4467305cd8200b11f7f61bca4) ;;
 *) return 1;;
 esac
 /bin/sh /usr/local/lib/nx-eye-af/verify-payload.sh || return 1
 [ -n "${HIB:-}" ] && [ -f "$state/armed" ] && [ ! -L "$state/armed" ] || return 1
 mode=$(cat /sys/power/bootmode) || return 1
 case "$mode" in 1|3) ;; *) return 1;; esac
 awk '$1=="/dev/mmcblk0p9" {found=1} END {exit !found}' /proc/swaps || return 1
 { IFS= read -r before && IFS= read -r policy; } < "$state/armed" || return 1
 IFS= read -r now < /proc/sys/kernel/random/boot_id || return 1
 [ -n "$now" ] && [ "$now" != "$before" ] && [ "$policy" = EYE ] || return 1
 "/usr/local/lib/nx-eye-af/claim.arm" "$state" "$now" "$mode" "$$" "$policy" "$lib"
}
cd / || exit 1
if eligible; then LD_PRELOAD=$lib exec "$app" "$@"; fi
exec "$app" "$@"
