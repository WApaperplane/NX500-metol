#!/bin/sh
# Root-backed, manually started service. No /opt or SD descriptor stays open.
set -u
root=
if [ "${1:-}" = --check-wireless-off ]; then root=${2:?}; fi
wireless_off() {
 for interface in mlan0 wlan0; do
  path=$root/sys/class/net/$interface
  [ -e "$path" ] || continue
  flags=$(cat "$path/flags" 2>/dev/null) || return 1
  case "$flags" in 0x[0-9a-fA-F]*) ;; *) return 1;; esac
  [ $((flags & 1)) = 0 ] || return 1
 done
 for device in "$root"/sys/class/rfkill/rfkill*; do
  [ -e "$device" ] || continue
  type=$(cat "$device/type" 2>/dev/null) || return 1
  case "$type" in wlan|bluetooth)
   [ "$(cat "$device/soft" 2>/dev/null)" = 1 ] || return 1;;
  esac
 done
 # Unknown live Bluetooth interfaces require explicit device preparation.
 for device in "$root"/sys/class/bluetooth/hci*; do
  [ ! -e "$device" ] || return 1
 done
 return 0
}
if [ "${1:-}" = --check-wireless-off ]; then wireless_off; exit $?; fi
[ "${1:-}" = --operator-ready ] || exit 64
state=/opt/usr/nx-eye-af
[ -d "$state" ] && [ ! -L "$state" ] && [ ! -e "$state/armed" ] && [ ! -L "$state/armed" ] || exit 1
mkdir /run/nx-eye-dispatch.lock || exit 1
trap 'rmdir /run/nx-eye-dispatch.lock' 0
printf 'phase=WAITING_FOR_WIRELESS_OFF\n' > "$state/dispatch-status.txt"
count=0
stable=0
while [ "$count" -lt 600 ]; do
 if wireless_off; then stable=$((stable+1)); else stable=0; fi
 if [ "$stable" -ge 5 ]; then
  printf 'phase=WIRELESS_OFF_DISPATCHING\n' > "$state/dispatch-status.txt"
  cd / || exit 1
  /bin/sh /usr/local/lib/nx-eye-af/arm-rescue.sh --operator-ready
  exit $?
 fi
 sleep 1
 count=$((count+1))
done
printf 'phase=TIMED_OUT_NOT_ARMED\n' > "$state/dispatch-status.txt"
exit 1
