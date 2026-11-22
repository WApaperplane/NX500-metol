#!/bin/sh
# test_p0_scripts.sh — 在 PC 上用模拟输入验证 p0-state.sh / p0-sample.sh
#
# 场景与对应缺口：
#   grow      录像中增长 → 停录后写尾 → 稳定        （基准）
#   plateau   录像中**长时间不增长**，fd 一直在      （不得判为 STABLE）
#   two       连续两段录像，track 目标必须切换        （不得跟错文件）
#   readonly  fd 指向 mp4 但 flags=RDONLY            （WFD_RW 应为 0）
#   nofd      fd 列表读取失败                        （记 NO_FD，不崩）
#   nocam     进程不存在                             （记 NO_PID）
#
# 用法：sh tools/test_p0_scripts.sh

set -u
HERE=$(cd "$(dirname "$0")" && pwd) || exit 1
SCRIPTS=$(cd "$HERE/../scripts" && pwd) || exit 1
STATE=$SCRIPTS/p0-state.sh
SAMPLE=$SCRIPTS/p0-sample.sh
SH=$(command -v sh)
[ -n "$SH" ] || { echo "需要 sh"; exit 1; }

FIX=${TMPDIR:-/tmp}/p0fix.$$
rm -rf "$FIX"; mkdir -p "$FIX/proc" "$FIX/mnt/mmc/DCIM/100PHOTO" || exit 1
trap 'rm -rf "$FIX"' EXIT INT TERM

printf '0.00 0.00\n' > "$FIX/proc/uptime"
printf 'MemTotal:  511580 kB\nMemFree:   5000 kB\nCached:    80000 kB\n' > "$FIX/proc/meminfo"

FAKE=$FIX/fakebb
cat > "$FAKE" <<'FAKEEOF'
UPT="$P0_TEST_ROOT/proc/uptime"
SC=${P0_SCENE:-grow}
A=/mnt/mmc/DCIM/100PHOTO/SAM_A.MP4
B=/mnt/mmc/DCIM/100PHOTO/SAM_B.MP4

ms() {
    set -- $(cat "$UPT")
    s=${1%%.*}; c=${1#*.}; c=${c#0}; c=${c:-0}
    echo $(( s * 1000 + c * 10 ))
}
size_of() {
    t=$(ms); p=$1
    case "$SC" in
    grow)
        if [ "$t" -lt 5000 ]; then echo $(( 1000000 + t * 100 )); else echo 1500000; fi ;;
    plateau)
        if [ "$t" -lt 1000 ]; then echo $(( 1000000 + t * 100 )); else echo 1100000; fi ;;
    two)
        case "$p" in
        *SAM_A*) echo $(( 1000000 + t * 100 )) ;;
        *)       echo $(( 2000000 + (t - 3000) * 100 )) ;;
        esac ;;
    *) echo 1000000 ;;
    esac
}
emit_fd() {  # $1=fd $2=path $3=rw|ro
    # 真实内核按 f_mode 显示权限：只写 l-wx、只读 lr-x、读写 lrwx。
    # 录像 fd 是只写，所以原正则 `l-w` 能匹配 `l-wx` —— 守卫在录像时有效。
    case "${3:-rw}" in
    ro)   printf 'lr-x------ 1 root root 64 Jan  1 00:00 %s -> %s\n' "$1" "$2" ;;
    rdwr) printf 'lrwx------ 1 root root 64 Jan  1 00:00 %s -> %s\n' "$1" "$2" ;;
    *)    printf 'l-wx------ 1 root root 64 Jan  1 00:00 %s -> %s\n' "$1" "$2" ;;
    esac
}

cmd=$1; shift
case "$cmd" in
pidof)
    [ "${P0_SCENE:-}" = nocam ] && exit 0
    echo 247; exit 0 ;;
devmem)
    echo "${P0_FAKE_MODE:-0x01}"; exit 0 ;;
sleep)
    set -- $(cat "$UPT")
    s=${1%%.*}; c=${1#*.}; c=${c#0}; c=${c:-0}
    t=$(( s * 1000 + c * 10 + 100 ))
    printf '%d.%02d 0.00\n' $(( t / 1000 )) $(( (t % 1000) / 10 )) > "$UPT"
    command sleep 0.002; exit 0 ;;
ls)
    if [ "$1" = "-l" ]; then
        t=$(ms)
        case "$SC" in
        nofd)     exit 1 ;;
        grow)     [ "$t" -lt 3000 ] && emit_fd 3 "$A" rw ;;
        plateau)  [ "$t" -lt 6000 ] && emit_fd 3 "$A" rw ;;
        readonly) [ "$t" -lt 3000 ] && emit_fd 3 "$A" ro ;;
        two)      if [ "$t" -lt 2000 ]; then emit_fd 3 "$A" rw
                  elif [ "$t" -ge 3000 ] && [ "$t" -lt 5000 ]; then emit_fd 4 "$B" rw; fi ;;
        rdwr)     [ "$t" -lt 3000 ] && emit_fd 3 "$A" rdwr ;;
        failmid)  if [ "$t" -lt 2000 ] || [ "$t" -ge 3000 ]; then emit_fd 3 "$A" rw
                  elif [ "$t" -ge 2000 ] && [ "$t" -lt 3000 ]; then exit 1; fi ;;
        statfail) [ "$t" -lt 2000 ] && emit_fd 3 "$A" rw ;;
        esac
        printf 'lr-x------ 1 root root 64 Jan  1 00:00 1 -> /dev/null\n'
        exit 0
    fi
    echo "$A"; exit 0 ;;
stat)
    # statfail：2000ms 后 stat 一律失败，验证「读不到不得累加稳定次数」
    if [ "$SC" = statfail ] && [ "$(ms)" -ge 2000 ]; then exit 1; fi
    size_of "$2"; exit 0 ;;
awk)
    case "$*" in *fdinfo*)
        # 模拟 awk '/^flags:/{print $2}' 只输出第二列
        if [ "$SC" = readonly ]; then echo "0100000"
        else echo "0100002"; fi
        exit 0 ;;
    esac
    exec awk "$@" ;;
grep|head|cat|mkdir|tail|wc|sed|tr)
    exec "$cmd" "$@" ;;
*) exit 0 ;;
esac
FAKEEOF
chmod +x "$FAKE" 2>/dev/null

BB="$SH $FAKE"
FAILS=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; FAILS=$((FAILS + 1)); }

reset_clock() { printf '0.00 0.00\n' > "$FIX/proc/uptime"; }

run_state() {
    # 必须先归零时钟：fake-bb 按 uptime 决定是否输出录像 fd，
    # 若沿用上一次采样结束后的 uptime（>3000），-l 分支只会输出 /dev/null
    reset_clock
    P0_TEST_ROOT="$FIX" P0_BB="$BB" P0_SCENE="${1:-grow}" P0_FAKE_MODE="${2:-0x01}" \
        "$SH" "$STATE"
}
run_sample() {
    reset_clock
    P0_TEST_ROOT="$FIX" P0_BB="$BB" P0_SCENE="$1" P0_STABLE_N=4 P0_STAT_EVERY=1 \
        "$SH" "$SAMPLE" "$2" "$FIX/o.tsv" >/dev/null 2>&1
    echo "$FIX/o.tsv"
}

# ---------------- state ----------------
out=$(run_state readonly)
st=$(printf '%s\n' "$out" | sed -n 's/^STATE=//p')
tr=$(printf '%s\n' "$out" | sed -n 's/^STATE_TRUE=//p')
rwn=$(printf '%s\n' "$out" | sed -n 's/^WFD_RW=//p')
gd0=$(printf '%s
' "$out" | sed -n 's/^WFD_GUARD=//p')
if [ "$rwn" = 0 ] && [ "$tr" = IDLE ] && [ "$gd0" = 0 ]; then
    ok "state 只读 fd(lr-x)：GUARD=0 RW=0 IDLE（只读不匹配 l-w）"
else bad "state 只读 fd 期望 G=0/RW=0/IDLE，得 G=$gd0 RW=$rwn TRUE=$tr"; fi

out=$(run_state grow)
g=$(printf '%s\n' "$out" | sed -n 's/^WFD_GUARD=//p')
rwn=$(printf '%s\n' "$out" | sed -n 's/^WFD_RW=//p')
tr=$(printf '%s\n' "$out" | sed -n 's/^STATE_TRUE=//p')
if [ "$g" = 1 ] && [ "$rwn" = 1 ] && [ "$tr" = RECORDING ]; then
    ok "state 只写 fd(l-wx)：GUARD=1 RW=1 RECORDING（守卫有效）"
else bad "state 只写 fd 期望 1/1/RECORDING，得 GUARD=$g RW=$rwn TRUE=$tr"; fi

out=$(run_state nocam)
st=$(printf '%s\n' "$out" | sed -n 's/^STATE=//p')
[ "$st" = NO_CAMERA ] && ok "state 无进程：NO_CAMERA" || bad "state 无进程 期望 NO_CAMERA，得 $st"

# ---------------- sample ----------------
T=$(run_sample grow 8)
for w in REC TAIL STABLE; do
    if awk -F'\t' -v x="$w" '$1 !~ /^#/ && $10==x{f=1} END{exit f?0:1}' "$T"; then
        ok "grow 出现 $w"
    else bad "grow 未见 $w"; fi
done
grow=$(awk -F'\t' '$1 !~ /^#/ && $10=="TAIL" && $8!="NA" {if(f=="")f=$8; l=$8; n++} END{if(n>=2&&l+0>f+0)print 1;else print 0}' "$T")
[ "$grow" = 1 ] && ok "grow 写尾期间大小仍在增长" || bad "grow 写尾期间大小未增长"

T=$(run_sample plateau 8)
# fd 在 6000ms 消失，STABLE 只允许出现在此后
firstst=$(awk -F'\t' '$1 !~ /^#/ && $10=="STABLE"{print $2; exit}' "$T")
if [ -z "$firstst" ]; then
    bad "plateau 未出现 STABLE（fd 消失后应能进入写尾稳定）"
elif [ "$firstst" -gt 6000 ]; then
    ok "plateau 首个 STABLE 在 ${firstst}ms，晚于 fd 消失（6000ms）"
else
    bad "plateau 在 ${firstst}ms 就判 STABLE，早于 fd 消失"
fi
if awk -F'\t' '$1 !~ /^#/ && $10=="STABLE" && $5+0>0{f=1} END{exit f?0:1}' "$T"; then
    bad "plateau 存在 rw>0 却标 STABLE 的样本"
else ok "plateau 无 rw>0 的 STABLE 样本"; fi
if awk -F'\t' '$1 !~ /^#/ && $10=="REC"{f=1} END{exit f?0:1}' "$T"; then
    ok "plateau 有 REC"
else bad "plateau 无 REC"; fi

T=$(run_sample two 7)
recs=$(awk -F'\t' '$1 !~ /^#/ && $10=="REC"{print $7}' "$T" | tr '\n' ' ')
n_rec=$(awk -F'\t' '$1 !~ /^#/ && $10=="REC"{n++} END{print n+0}' "$T")
if [ "$n_rec" -ge 2 ]; then
    ok "two 出现 $n_rec 次 REC，tgt 序列: $recs"
else bad "two 期望 ≥2 次 REC，得 $n_rec（$recs）"; fi
if [ "$n_rec" -ge 2 ] && [ "$(printf '%s\n' $recs | sort -u | wc -l)" -ge 2 ]; then
    ok "two 跟踪目标已切换"
else bad "two 跟踪目标未切换（$recs）"; fi

# rdwr：读写 fd，l-w 不匹配但 flags 可写 -> GUARD=0 RW=1，这才是真实漏检面
out=$(run_state rdwr)
' ' ')" >&2
g=$(printf '%s
' "$out" | sed -n 's/^WFD_GUARD=//p')
rwn=$(printf '%s
' "$out" | sed -n 's/^WFD_RW=//p')
tr=$(printf '%s
' "$out" | sed -n 's/^STATE_TRUE=//p')
if [ "$g" = 0 ] && [ "$rwn" = 1 ] && [ "$tr" = RECORDING ]; then
    ok "state 读写 fd(lrwx)：GUARD=0 RW=1 RECORDING（守卫漏检面）"
else bad "state 读写 fd 期望 0/1/RECORDING，得 G=$g RW=$rwn TRUE=$tr"; fi

# failmid：录像中途 fd 查询失败，不得因此判 TAIL
T=$(run_sample failmid 6)
if awk -F'	' '$1 !~ /^#/ && $10=="NO_FD"{f=1} END{exit f?0:1}' "$T"; then
    ok "failmid 记录 NO_FD"
else bad "failmid 未记录 NO_FD"; fi
# NO_FD 之后、fd 恢复之前不应出现 TAIL
bad_order=$(awk -F'	' '$1!~/^#/ { if ($10=="NO_FD") nf=1; if (nf && $10=="TAIL" && $5!="UNKNOWN") print 1 }' "$T" | head -1)
if [ -z "$bad_order" ]; then
    ok "failmid 查询失败期间未推进为 TAIL"
else bad "failmid 查询失败后出现了 TAIL（应保持原状态）"; fi

# statfail：收尾时 stat 失败，不得累加稳定次数
T=$(run_sample statfail 8)
if awk -F'	' '$1 !~ /^#/ && $8=="UNKNOWN"{f=1} END{exit f?0:1}' "$T"; then
    ok "statfail 记录 bytes=UNKNOWN"
else bad "statfail 未记 bytes=UNKNOWN"; fi
if awk -F'	' '$1 !~ /^#/ && $10=="STABLE"{f=1} END{exit f?0:1}' "$T"; then
    bad "statfail 在 stat 全失败时仍判 STABLE"
else ok "statfail stat 失败期间未误判 STABLE"; fi

T=$(run_sample nofd 3)
if awk -F'\t' '$1 !~ /^#/ && $10=="NO_FD"{f=1} END{exit f?0:1}' "$T"; then
    ok "nofd 记录 NO_FD"
else bad "nofd 未记录 NO_FD"; fi
if awk -F'\t' '$1 !~ /^#/ && $10=="REC"{f=1} END{exit f?0:1}' "$T"; then
    bad "nofd 不应出现 REC"
else ok "nofd 未误报 REC"; fi

echo
if [ "$FAILS" = 0 ]; then echo "全部通过"; exit 0; fi
echo "失败 $FAILS 项"; exit 1
