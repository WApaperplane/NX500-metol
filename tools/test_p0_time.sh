#!/bin/sh
# test_p0_time.sh — p0_now_ms 的逐项验证
#
# 覆盖复查点名的取值：.00 / .01 / .09 / .10 / .99，以及跨秒与异常输入。
# 判据是「精确值」，不是「单调」——单调可以由 max() 伪造，精确值不能。
#
# 用法：sh tools/test_p0_time.sh

set -u
HERE=$(cd "$(dirname "$0")" && pwd) || exit 1
LIB=$(cd "$HERE/../scripts" && pwd)/p0-time.sh
[ -f "$LIB" ] || { echo "找不到 $LIB"; exit 1; }

. "$LIB"

FIX=${TMPDIR:-/tmp}/p0time.$$
rm -rf "$FIX"; mkdir -p "$FIX" || exit 1
trap 'rm -rf "$FIX"' EXIT INT TERM
F=$FIX/uptime

FAILS=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; FAILS=$((FAILS + 1)); }

# $1=内容 $2=期望ms
chk() {
    printf '%s\n' "$1" > "$F"
    got=$(p0_now_ms "$F"); rc=$?
    if [ "$rc" = 0 ] && [ "$got" = "$2" ]; then
        ok "$1 → $2 ms"
    else
        bad "$1 期望 $2 ms，得 $got（rc=$rc）"
    fi
}

# 整数秒
chk "100.00 1.00" 100000
# 关键四项
chk "100.01 1.00" 100010
chk "100.09 1.00" 100090
chk "100.10 1.00" 100100
chk "100.99 1.00" 100990
# 跨秒
chk "100.99 1.00" 100990
chk "101.00 1.00" 101000
chk "101.01 1.00" 101010
# 全零
chk "0.00 0.00" 0
chk "1.00 0.00" 1000
# 一位小数（部分内核可能这样）
chk "100.5 1.00" 100500
# 四位小数（截断）
chk "100.1234 1.00" 100123
# 无小数
chk "42 1.00" 42000

# 异常输入：必须 rc≠0，且输出 0
for v in "" "abc" "12.3x" ".." ; do
    printf '%s\n' "$v" > "$F"
    got=$(p0_now_ms "$F"); rc=$?
    if [ "$rc" != 0 ]; then
        ok "非法输入 '$v' → rc=$rc（拒绝）"
    else
        bad "非法输入 '$v' 应拒绝，却返回 rc=0 got=$got"
    fi
done

# 文件不存在
got=$(p0_now_ms "$FIX/nothere"); rc=$?
[ "$rc" != 0 ] && ok "文件不存在 → rc=$rc" || bad "文件不存在应拒绝"

# 单调性（真实序列，不是 max 伪造）
{
    for s in 100 101 102; do
        for f in 00 01 05 09 10 50 98 99; do
            printf '%d.%s 1.00\n' "$s" "$f"
        done
    done
} > "$FIX/seq"
prev=-1; mono=1
while read -r line; do
    printf '%s\n' "$line" > "$F"
    v=$(p0_now_ms "$F") || continue
    if [ "$v" -le "$prev" ]; then mono=0; echo "  非单调: $line → $v (prev=$prev)"; fi
    prev=$v
done < "$FIX/seq"
[ "$mono" = 1 ] && ok "24 个真实取值序列严格单调" || bad "序列出现非单调"

echo
if [ "$FAILS" = 0 ]; then echo "全部通过"; exit 0; fi
echo "失败 $FAILS 项"; exit 1
