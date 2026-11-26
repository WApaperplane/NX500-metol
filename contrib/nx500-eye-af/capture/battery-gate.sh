#!/bin/sh
# Battery evidence for the capture operator. Pure function over a sysfs root.
#
# This body has been seen reporting a raw hardware value (1460) in
# power_supply/battery/capacity, so the field is only a percentage when it is
# 0..100. Anything else cannot be used as a charge gate and must be replaced by
# an explicit operator claim (--battery-confirmed-full).
#
# exit 0 = pass, 3 = needs the operator claim, 4 = readable and too low.
set -u
root=${1:-/sys/class/power_supply/battery}
confirmed=${2:-0}
cap=$(cat "$root/capacity" 2>/dev/null) || cap=
st=$(cat "$root/status" 2>/dev/null) || st=
volt=$(cat "$root/voltage_now" 2>/dev/null) || volt=
printf 'capacity=%s\nstatus=%s\nvoltage_now=%s\nconfirmed_full=%s\n' "$cap" "$st" "$volt" "$confirmed"
case "$cap" in
    ''|*[!0-9]*)
        [ "$confirmed" = 1 ] || { echo BATTERY_UNREADABLE_NEEDS_CONFIRMATION >&2; exit 3; }
        ;;
    *)
        if [ "$cap" -le 100 ]; then
            [ "$cap" -ge 95 ] || { echo FULL_BATTERY_REQUIRED >&2; exit 4; }
        else
            [ "$confirmed" = 1 ] || { echo BATTERY_VALUE_IS_NOT_A_PERCENT >&2; exit 3; }
        fi
        ;;
esac
exit 0
