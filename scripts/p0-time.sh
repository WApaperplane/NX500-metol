#!/bin/sh
# p0-time.sh — /proc/uptime 第一字段 → 毫秒整数（被 p0-sample.sh source）
#
# 存在的理由：先前用「去前导零 + 补尾零」的字符串技巧，把小数部分当成了
# 十进制整数的十倍处理，导致
#     100.01 → 100100 （应为 100010）
#     100.09 → 100900 （应为 100090）
# 正确关系是：小数部分有 n 位，代表 10^(3-n) 毫秒的单位。
#     n=1  .5    → 5 × 100 = 500 ms
#     n=2  .05   → 5 × 10  = 50  ms
#     n=2  .50   → 50 × 10 = 500 ms
#     n=3  .123  → 123 ms（截断）
#
# 用法：
#   . ./p0-time.sh
#   ms=$(p0_now_ms)            # 读 /proc/uptime
#   ms=$(p0_now_ms "$file")    # 读指定文件（测试用）
#
# 返回：成功时输出毫秒整数、退出 0；文件读不到或内容非法时输出 0、退出 1。
#      调用方**必须**检查退出码，不能把 0 当成合法时间。

p0_now_ms() {
    _src=${1:-/proc/uptime}
    [ -r "$_src" ] || { echo 0; return 1; }
    if ! read -r up _ < "$_src" 2>/dev/null; then
        echo 0
        return 1
    fi
    case "$up" in
    '' | *[!0-9.]*)
        echo 0
        return 1
        ;;
    esac
    # 小数点后必须全是数字（挡掉 ".." / "1." 之类）
    case "${up#*.}" in
    *[!0-9]*)
        echo 0
        return 1
        ;;
    esac

    _i=${up%.*}
    _raw=${up#*.}
    # 没有小数点时 up#*. 等于原串
    [ "$_raw" = "$up" ] && _raw=0
    [ -n "$_i" ] || _i=0

    _n=${#_raw}
    # 去掉全部前导零（"09"→"9"，"00"→""）
    _f=$_raw
    while :; do
        _t=${_f#0}
        [ "$_t" = "$_f" ] && break
        _f=$_t
    done
    [ -n "$_f" ] || _f=0

    case "$_n" in
    1) _frac=$((_f * 100)) ;;
    2) _frac=$((_f * 10)) ;;
    3) _frac=$((_f)) ;;
    4) _frac=$((_f / 10)) ;;
    *) _frac=0 ;;
    esac

    echo $((_i * 1000 + _frac))
    return 0
}
