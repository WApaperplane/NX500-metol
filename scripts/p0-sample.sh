#!/bin/sh
# p0-sample.sh — P0 时间线采样（只读）
#
# 输出列：n  t_ms  pid  guard  rw  fd  tgt  bytes  avail  note
#   t_ms   p0_now_ms() 的毫秒整数；读失败记 UNKNOWN
#   guard  原守卫正则 `l-w` 数到的行数（只写 fd 显示 l-wx，能匹配）
#   rw     可写且指向 DCIM/*.mp4 的 fd 数（读 fdinfo flags）；读取失败记 UNKNOWN
#   bytes  跟踪文件字节数；stat 失败记 UNKNOWN
#   note   REC / TAIL / STABLE / NO_PID / NO_FD / FD_UNKNOWN / STAT_UNKNOWN
#
# 三条硬规则：
#   1. 稳定计数**只在 fd 消失后**累加；录像期大小不变不计数
#   2. 跟踪目标取自 FD 的 readlink 目标，不按目录 mtime 猜
#   3. **任何「读不到」都记 UNKNOWN，且不推进状态机**：
#      FD 读不到不能当作「没有录像」；stat 读不到不能复用旧大小继续累加
#
# 参数：<秒> [输出文件]
# 环境：P0_TEST_ROOT / P0_BB / P0_STABLE_N(默认8) / P0_STAT_EVERY(默认3)

BB=${P0_BB:-/opt/usr/nx-ks/busybox}
SELF=${0%/*}
[ "$SELF" != "$0" ] || SELF=.
. "$SELF/p0-time.sh" || exit 1

R=${P0_TEST_ROOT:-}
PROC=$R/proc
SD=$R/mnt/mmc
DUR=${1:-60}
OUT=${2:-/tmp/nx-p0/sample.tsv}
STABLE_N=${P0_STABLE_N:-8}
STAT_EVERY=${P0_STAT_EVERY:-3}

$BB mkdir -p "${OUT%/*}" 2>/dev/null
: > "$OUT"
printf '# n\tt_ms\tpid\tguard\trw\tfd\ttgt\tbytes\tavail\tnote\n' >> "$OUT"

DUR_MS=$((DUR * 1000))
t0=""
n=0
state=idle
track_fd=-
track_tgt=-
track_path=""
last_b=NA
stable=0
guard=0
rw=0

while :; do
    n=$((n + 1))

    t=$(p0_now_ms "$PROC/uptime"); tok=$?
    if [ "$tok" = 0 ]; then
        [ -n "$t0" ] || t0=$t
        rel=$((t - t0))
        [ "$rel" -ge "$DUR_MS" ] && break
    else
        rel=UNKNOWN
    fi
    [ "$n" -gt $((DUR * 20)) ] && break

    note=""
    pid=$($BB pidof di-camera-app 2>/dev/null | $BB awk '{print $1}')
    pairs=""
    guard=0
    rw=0
    cand=0
    readable=1

    if [ -z "$pid" ]; then
        note=NO_PID
        readable=0
        pid=NA
    else
        listing=$($BB ls -l "$PROC/$pid/fd" 2>/dev/null)
        if [ -z "$listing" ]; then
            note=NO_FD
            readable=0
        else
            guard=$(printf '%s\n' "$listing" | $BB grep -aci 'l-w.*DCIM/.*\.mp4')
            [ -n "$guard" ] || guard=0
            pairs=$(printf '%s\n' "$listing" | $BB awk '
                match($0, /[0-9]+ -> /) {
                    fd = substr($0, RSTART, RLENGTH - 4)
                    target = substr($0, RSTART + RLENGTH)
                    if (tolower(target) ~ /dcim\/.*\.mp4$/) print fd "\t" target
                }')
        fi
    fi

    if [ "$readable" = 1 ]; then
        oldifs=$IFS
        IFS='
	'
        set -- $pairs
        IFS=$oldifs
        first_fd=-
        first_tgt=-
        first_path=""
        while [ $# -ge 2 ]; do
            cfd=$1
            ctarget=$2
            shift 2
            cand=$((cand + 1))
            flags=$($BB awk '/^flags:/{print $2}' "$PROC/$pid/fdinfo/$cfd" 2>/dev/null)
            case "$flags" in '' | *[!0-7]*) continue ;; esac
            acc=$(( 0$flags & 3 ))
            [ "$acc" -ne 0 ] || continue
            rw=$((rw + 1))
            if [ "$first_fd" = "-" ]; then
                first_fd=$cfd
                first_tgt=${ctarget##*/}
                first_path=$ctarget
            fi
        done

        # 有候选 fd 但 flags 一个都读不到：不能当成 rw=0
        if [ "$cand" -gt 0 ] && [ "$rw" -eq 0 ]; then
            note=FD_UNKNOWN
            readable=0
            rw=UNKNOWN
        fi
    fi

    if [ "$readable" = 1 ]; then
        if [ "$rw" -gt 0 ]; then
            if [ "$state" != record ]; then
                note=REC
                stable=0
                last_b=NA
            fi
            state=record
            track_fd=$first_fd
            track_tgt=$first_tgt
            track_path=$first_path
        else
            if [ "$state" = record ]; then
                state=tail
                stable=0
                note=TAIL
            elif [ "$state" = tail ]; then
                note=TAIL
            elif [ "$state" != done ]; then
                state=idle
            fi
        fi
    fi

    bytes=NA
    if [ -n "$track_path" ] && [ "$state" != done ]; then
        if [ "$state" = record ] || [ "$note" = TAIL ] || [ $((n % STAT_EVERY)) -eq 0 ]; then
            b=$($BB stat -c %s "$track_path" 2>/dev/null)
            if [ -n "$b" ]; then
                bytes=$b
            else
                # 读不到：不复用旧值，也不累加稳定次数
                bytes=UNKNOWN
                stable=0
                [ "$readable" = 1 ] && note=STAT_UNKNOWN
            fi
        else
            bytes=$last_b
        fi
    fi

    if [ "$state" = tail ] && [ "$bytes" != NA ] && [ "$bytes" != UNKNOWN ]; then
        if [ "$bytes" = "$last_b" ]; then
            stable=$((stable + 1))
        else
            stable=0
        fi
        if [ "$stable" -ge "$STABLE_N" ]; then
            state=done
            note=STABLE
        fi
    fi
    [ "$bytes" = NA ] || [ "$bytes" = UNKNOWN ] || last_b=$bytes

    avail=$($BB awk '/^MemFree:/{a=$2} /^Cached:/{c=$2} END{printf "%d", a+c}' "$PROC/meminfo" 2>/dev/null)
    [ -n "$avail" ] || avail=NA

    printf '%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$n" "$rel" "$pid" "$guard" "$rw" "$track_fd" "$track_tgt" "$bytes" "$avail" "$note" >> "$OUT"

    $BB sleep 0.1
done

printf '# done n=%d dur_s=%d state=%s\n' "$n" "$DUR" "$state" >> "$OUT"
$BB cat "$OUT"
