#!/bin/sh
# nx-timing.sh — LUT / FilmLab 链路分段计时
#
# 用途：给"还能不能更快"提供数据，而不是靠猜。三个模式：
#
#   sh /tmp/nx-timing.sh probe
#       只读（不改 A18 / 登记表 / PW）。测 st / prefman / fork / cube2lut 等原子成本。
#       唯一的写是 lut.sh list 会顺带维护 order 索引（内容没变则不落盘）。
#
#   sh /tmp/nx-timing.sh apply <look名>
#       真的换表（会改 A18 + 登记表）。带 NX_TIMING 分段，输出各阶段耗时。
#
#   sh /tmp/nx-timing.sh off
#       真的还原原厂。
#
#   sh /tmp/nx-timing.sh preview <配方key>
#       真的预览（画面会变）。带分段。
#
# 计时用 /proc/uptime + 内建 read，不 fork；误差约 10ms（uptime 本身精度）。
# 只有 probe 模式保证零副作用。

D=/opt/usr/nx-ks
BB=$D/busybox
LAB=/mnt/mmc/looks

_now() {
    read -r _u _ < /proc/uptime 2>/dev/null || { NOW=0; return 1; }
    _i=${_u%.*}; _raw=${_u#*.}
    [ "$_raw" = "$_u" ] && _raw=0
    [ -n "$_i" ] || _i=0
    # 先记位数再去前导零；去零是必须的 —— $((09)) 在 POSIX sh 里按八进制解析，
    # 会报 Illegal number 并让整个脚本退出，而 uptime 的 .05/.08 很常见。
    _n=${#_raw}
    _f=$_raw
    while :; do
        _t=${_f#0}
        [ "$_t" = "$_f" ] && break
        _f=$_t
    done
    [ -n "$_f" ] || _f=0
    case "$_n" in
        1) _f=$((_f * 100)) ;;
        2) _f=$((_f * 10)) ;;
        3) ;;
        *) _f=0 ;;
    esac
    NOW=$((_i * 1000 + _f))
    return 0
}

run() {   # run <label> <cmd...>
    _l=$1; shift
    _now; _a=$NOW
    "$@" >/dev/null 2>&1; _rc=$?
    _now
    printf '%-26s %6dms  rc=%s\n' "$_l" "$((NOW - _a))" "$_rc"
}

mode=${1:-probe}
case "$mode" in
probe)
    echo "=== 基线（各跑 1 次，短操作建议看多次结果的中位） ==="
    run "busybox true (fork)"        "$BB" true
    run "sh -c true (fork+exec)"     sh -c true
    run "prefman get 0x0a704"        prefman get 0 0x0a704 l
    run "st getusr 20"               st cap capdtm getusr 20
    run "st getusr 20 (2nd)"         st cap capdtm getusr 20
    run "st getusr 20 (3rd)"         st cap capdtm getusr 20
    run "lut.sh status"              sh "$D/lut.sh" status
    run "lut.sh list"                sh "$D/lut.sh" list

    echo
    echo "=== pwsend 进程启动下限（dry-run，不含 MCB 初始化） ==="
    echo "  注意：pwsend 的 dry-run 在 mcb_bring_up() 之前就 return，"
    echo "        所以这里测到的只是 fork+参数解析，不是真发时的启动成本。"
    echo "        真实值 = preview/commit 里 pw_direct 段 减去 8×16ms(svdm) 与 7×20ms(sleep)。"
    if [ -x "$D/pwsend.arm" ]; then
        run "pwsend seq 1条 (dry)"   "$D/pwsend.arm" seq 0x134 100
        run "pwsend seq 8条 (dry)"   "$D/pwsend.arm" seq 0x134 100 0x135 100 0x136 100 0x137 10 0x138 10 0x139 10 0x13A 10 0x13B 1
    else
        echo "  $D/pwsend.arm 缺失"
    fi

    echo
    echo "=== cube2lut 转换成本（写到 /tmp，不动库） ==="
    if [ -x "$D/lut/cube2lut.arm" ]; then
        c=
        for f in "$LAB"/*.cube; do [ -f "$f" ] && { c=$f; break; }; done
        if [ -n "$c" ]; then
            echo "  样本: $c ($($BB wc -c < "$c" | $BB tr -d ' ')B)"
            run "cube2lut"           "$D/lut/cube2lut.arm" "$c" /tmp/nx-timing-out.bin
            rm -f /tmp/nx-timing-out.bin
            run "cksum .cube"        "$BB" cksum "$c"
        else
            echo "  $LAB 下没有 .cube，跳过转换与校验和测量"
        fi
    else
        echo "  cube2lut.arm 缺失"
    fi

    echo
    echo "=== 结论怎么读 ==="
    echo "  · st getusr/setusr 在 1.2-1.4s：确认瓶颈是档位通道，不是 fork 或 prefman"
    echo "  · pwsend dry 只反映进程启动下限；真正要不要常驻，看 preview 的 pw_direct 段"
    echo "  · lut.sh list 会顺带维护 order 索引（内容没变则不写盘），不算零副作用"
    ;;

apply)
    [ -n "${2:-}" ] || { echo "用法: sh $0 apply <look名>"; exit 2; }
    echo ">>> 真的要换表：$2（会改 A18 与三张登记表）"
    NX_TIMING=1 sh "$D/lut.sh" apply "$2"
    ;;

off)
    echo ">>> 真的要还原原厂"
    NX_TIMING=1 sh "$D/lut.sh" off
    ;;

preview)
    [ -n "${2:-}" ] || { echo "用法: sh $0 preview <配方key>"; exit 2; }
    echo ">>> 真的要预览：$2（画面会变）"
    FILMLAB_PW=1 NX_TIMING=1 sh "$D/filmlab.sh" preview "$2"
    ;;

commit)
    [ -n "${2:-}" ] || { echo "用法: sh $0 commit <配方key>"; exit 2; }
    echo ">>> 真的要提交：$2（会写槽 + 恢复档位）"
    FILMLAB_PW=1 NX_TIMING=1 sh "$D/filmlab.sh" commit "$2"
    ;;

*)
    echo "usage: sh $0 probe | apply <look> | off | preview <recipe> | commit <recipe>"
    exit 2
    ;;
esac
