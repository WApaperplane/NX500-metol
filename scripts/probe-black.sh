#!/bin/sh
# probe-black.sh —— 黑屏归因的最小实验（自恢复 + 屏上打标记）
#
# 用法: sh /tmp/probe-black.sh A|B|C|all [hold_seconds]
#   A  只走 clean-log-prepare（写 A18 + 三张登记表 + video_enable，不碰 gamma）
#   B  只走 gamma-reload.sh log（跑 native gamma 事务，不做 prepare）
#   C  完整 toggle（prepare + gamma），即用户按「V-Log 开关」的路径
#   all 依次 A → B → C，每步之间恢复
#
# 每步先在屏幕上弹一个字母（2 秒），再执行 —— 屏幕黑掉时弹窗仍然可见
# （它是独立的 X 窗口，不依赖 ISP），所以能对上"哪一步黑的"。
# 每步结束都恢复：dyn-restore → vlog.sh off → filmlab neutral。
set -u
D=/opt/usr/nx-ks
BB=$D/busybox
G=$D/gamma
POP=$D/popup_timeout
step=${1:-A}
hold=${2:-8}
log=/tmp/probe-black.log

say() { printf '%s %s\n' "$($BB date +%H:%M:%S)" "$*" | tee -a "$log"; }

restore() {
    say "  restore dyn-restore"
    "$G/bin/nx_regtabwrite.arm" dyn-restore >/dev/null 2>&1
    say "  restore vlog off"
    "$D/vlog.sh" off >/dev/null 2>&1
    say "  restore pw neutral"
    FILMLAB_PW=1 "$D/filmlab.sh" neutral >/dev/null 2>&1
    a18=$($G/bin/nx_slotwrite.arm read 0x81119F00 /tmp/pb18.bin >/dev/null 2>&1; $BB md5sum /tmp/pb18.bin | $BB cut -d' ' -f1)
    say "  restore done A18=$a18"
}

pop() {
    [ -x "$POP" ] || return 0
    nice -n +10 "$POP" " $1 " "${2:-2}" >/dev/null 2>&1
}

one() {   # $1 = A|B|C
    pop " $1 " 2
    say "STEP $1 begin"
    case "$1" in
    A) NXL_LUT_DEFER_RELOAD=1 /bin/sh "$G/lut.sh" clean-log-prepare 2>&1 | $BB tail -4 ;;
    B) "$G/gamma-reload.sh" log 2>&1 | $BB tail -6 ;;
    C) "$D/vlog.sh" toggle 2>&1 | $BB tail -3 ;;
    esac
    say "STEP $1 hold ${hold}s —— 看屏幕"
    $BB sleep "$hold"
    say "STEP $1 end"
    restore
}

trap 'restore' 1 2 15
: > "$log"
say "BEGIN step=$step hold=${hold}s"

case "$step" in
all)
    one A
    $BB sleep 2
    one B
    $BB sleep 2
    one C
    ;;
A|B|C) one "$step" ;;
*) say "BAD STEP"; exit 2 ;;
esac
say "ALL DONE"
