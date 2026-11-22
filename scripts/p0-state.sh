#!/bin/sh
# p0-state.sh — 只读观测：记录「原守卫看到什么」与「fdinfo 显示什么」
#
# 不修改任何状态、不申请锁、不写曲线。
#
# 两类判据必须分开报，不能混为一谈：
#   WFD_GUARD  用原守卫正则 `l-w.*DCIM/.*\.mp4` 数到的行数
#              —— 只反映「正则是否匹配该行文本」，不代表 fd 打开模式
#   WFD_RW     遍历 fd 列表，取指向 DCIM/*.mp4（忽略大小写）的 fd，
#              再读 /proc/<pid>/fdinfo/<fd> 的 flags，判断 O_WRONLY/O_RDWR
#              —— 这才是「可写录像 fd」的判据
#
# STATE       按 WFD_GUARD 得出的结论（忠实复刻守卫行为）
# STATE_TRUE  按 WFD_RW 得出的结论（实际是否在写 mp4）
# GUARD_BLIND 两者结论不一致时为 1
#
# 关于 `l-w`：`-` 是字面量，不是通配。对 `lrwx------ ... -> x.mp4` 这种行
# 确实匹配不上；但不断言「在任何情况下都匹配不到」——ls 输出格式与 fd 形态
# 可能因实现而异。本脚本只报观测值。
#
# 可测性：P0_TEST_ROOT 指向 fixture 根；P0_BB 替换 busybox。

BB=${P0_BB:-/opt/usr/nx-ks/busybox}
R=${P0_TEST_ROOT:-}
PROC=$R/proc
SD=$R/mnt/mmc
MODE_ADDR=${P0_MODE_ADDR:-0x817dccb8}

emit() {
    # 1=STATE 2=STATE_TRUE 3=GUARD 4=RW 5=BLIND 6=PID 7=MODE 8=BYTES 9=AVAIL 10=RC
    printf 'STATE=%s\nSTATE_TRUE=%s\nWFD_GUARD=%s\nWFD_RW=%s\nGUARD_BLIND=%s\nPID=%s\nMODE=%s\nMP4_BYTES=%s\nAVAIL=%s\nRC=%s\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${10}"
    exit "${10}"
}

avail=$($BB awk '/^MemFree:/{a=$2} /^Cached:/{c=$2} END{printf "%d", a+c}' "$PROC/meminfo" 2>/dev/null)
[ -n "$avail" ] || avail=NA

pid=$($BB pidof di-camera-app 2>/dev/null | $BB awk '{print $1}')
if [ -z "$pid" ]; then
    emit NO_CAMERA NO_CAMERA NA NA NA NA NA NA "$avail" 1
fi

listing=$($BB ls -l "$PROC/$pid/fd" 2>/dev/null)
if [ -z "$listing" ]; then
    emit NO_RECORDING_STATE NO_RECORDING_STATE NA NA NA "$pid" NA NA "$avail" 1
fi

# 原守卫正则（忠实复刻，不改）
guard=$(printf '%s\n' "$listing" | $BB grep -aci 'l-w.*DCIM/.*\.mp4')
[ -n "$guard" ] || guard=0

# 抽 `N -> /path`，只留指向 DCIM 下 mp4 的（忽略大小写）
pairs=$(printf '%s\n' "$listing" | $BB awk '
    match($0, /[0-9]+ -> /) {
        fd = substr($0, RSTART, RLENGTH - 4)
        target = substr($0, RSTART + RLENGTH)
        if (tolower(target) ~ /dcim\/.*\.mp4$/) print fd "\t" target
    }')

rw=0
rwbytes=NA
rwtarget=NA
oldifs=$IFS
IFS='
	'
set -- $pairs
IFS=$oldifs
while [ $# -ge 2 ]; do
    fd=$1
    target=$2
    shift 2
    flags=$($BB awk '/^flags:/{print $2}' "$PROC/$pid/fdinfo/$fd" 2>/dev/null)
    case "$flags" in ''|*[!0-7]*) continue ;; esac
    # 八进制 flags；低两位 0=RDONLY 1=WRONLY 2=RDWR
    acc=$(( 0$flags & 3 ))
    [ "$acc" -ne 0 ] || continue
    rw=$((rw + 1))
    rwtarget=$target
    b=$($BB stat -c %s "$target" 2>/dev/null)
    [ -n "$b" ] && rwbytes=$b
done

# 目录里最新的 mp4，仅作对照；不作为「正在写」的证据
newest=NA
f=$($BB ls -t "$SD"/DCIM/*/*.[Mm][Pp]4 2>/dev/null | $BB head -n 1)
[ -n "$f" ] && newest=$($BB stat -c %s "$f" 2>/dev/null)
[ -n "$newest" ] || newest=NA
[ "$rwbytes" = NA ] || newest=$rwbytes

blind=0
[ "$guard" -gt 0 ] && [ "$rw" -eq 0 ] && blind=1
[ "$guard" -eq 0 ] && [ "$rw" -gt 0 ] && blind=1

if [ "$guard" -gt 0 ]; then
    emit RECORDING RECORDING "$guard" "$rw" "$blind" "$pid" NA "$newest" "$avail" 6
fi

mode=$($BB devmem "$MODE_ADDR" 8 2>/dev/null)
if [ -z "$mode" ]; then
    emit NO_MODE NO_MODE "$guard" "$rw" "$blind" "$pid" NA "$newest" "$avail" 1
fi
if [ "$((mode))" -ne 1 ]; then
    emit UNSUPPORTED_MODE UNSUPPORTED_MODE "$guard" "$rw" "$blind" "$pid" "$mode" "$newest" "$avail" 6
fi

if [ "$rw" -gt 0 ]; then
    emit IDLE RECORDING "$guard" "$rw" "$blind" "$pid" "$mode" "$newest" "$avail" 0
fi
emit IDLE IDLE "$guard" "$rw" "$blind" "$pid" "$mode" "$newest" "$avail" 0
