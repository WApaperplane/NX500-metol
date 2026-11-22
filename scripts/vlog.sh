#!/bin/sh
# vlog.sh — V-Log 曲线的菜单后端（gui_vlog.NX500 / gui_evwifi.NX500 调用）
#
# 动词：
#   toggle   一键切：没曲线就上曲线，有曲线就回原厂（幂等，UI 主按钮）
#   on       强制上曲线（gamma-reload.sh log，要求当前是原厂）
#   off      强制回原厂 gamma
#   status   打印曲线状态块，不弹窗（命令行/诊断用）
#   gate     跑 V-Log 放行检查，弹窗只给结论，明细落 /tmp/nx-p0/vlog-gate.log
#
# ── 引擎位置 ──────────────────────────────────────────────────────────
# 曲线引擎是 RC5 的 nxl-gamma-reload.arm + lut.sh 一族，**默认没有装进机身**
# （用户明确「暂不安装 RC5」）。这里只认 $D/gamma/ 这一处，不做自动回退去
# SD 卡上跑 —— 菜单误点一下就会改相机 gamma，不能靠"恰好卡在机器里"生效。
#
#   $D/gamma/gamma-reload.sh   引擎入口（连同 bin/ lut.sh data/ 整棵）
#
# 装上之后 gamma-reload.sh 会自己按 $D=脚本目录 校验五个组件 sha256
# （OPS / GAMMA_RELOAD / USB_GUARD / IDLE / REFRESH），所以必须整棵搬，
# 不能只拷一个 .arm。可用 VLOG_ENGINE 覆盖，仅供调试。
#
# ── 为什么 gate 和 toggle 分开 ────────────────────────────────────────
# 放行检查管的是资源层与效果层（A18 回原厂 / PW 中性 / 未插 USB），
# 曲线开关只动 gamma 侧。合成一个按钮就成了「一次点击改两处状态」，
# 出问题无法归因。

BB=/opt/usr/nx-ks/busybox
D=/opt/usr/nx-ks
ARM=$D/vlog-arm.sh
ENGINE=${VLOG_ENGINE:-$D/gamma/gamma-reload.sh}
POP=$D/popup_timeout
GATELOG=/tmp/nx-p0/vlog-gate.log
RUN=/tmp/nx-lut-lite

say() { printf '%s=%s\n' "$1" "$2"; }

pop() {
    # 面板在跑时走 nxpopup.sh：面板自己画横幅，一定在最上层；
    # popup_timeout 是独立窗口，会被面板盖住（用户看不到 = "点了没反应"）。
    if [ -x "$D/nxpopup.sh" ]; then "$D/nxpopup.sh" "$1" "${2:-3}"; return 0; fi
    # nice +10：弹窗别跟 di-camera-app 抢单核（社区约定）
    [ -x "$POP" ] || return 0
    nice -n +10 "$POP" " $1 " "${2:-3}" >/dev/null 2>&1
}

engine_ok() { [ -f "$ENGINE" ] && [ ! -L "$ENGINE" ] && [ -x "$ENGINE" ]; }

# ★ RC5 的顺序瑕疵：gamma-reload.sh 的 status 分支在 `mkdir -p $RUN` 之前
#   就 exec 了，而 $RUN=/tmp/nx-lut-lite 是 tmpfs，重启即空。
#   ⇒ 重启后第一次 status 必报 IDLE_RAM_REPORT_UNAVAILABLE（rc=1）。
#   这里先补目录，绕开它，不改 RC5 的脚本（改了哈希链就断）。
#
# ★ 2026-11-20 血账：`$GAMMA restore` 会往 /mnt/mmc/NXDEMO51/ 写审计文件
#   （二进制里写死 `"/mnt/mmc/NXDEMO51/GAMMA_R11Q_%llu_%d_%08x"`），而这个
#   目录**默认不存在**。于是 restore 报 VIDEO_AUDIT_DIRECTORY_FAILED 直接退出，
#   留下半状态：clean-log.policy=READY + A18 已被改 + DYN24 全指 A18 + gamma=FACTORY
#   ⇒ 取景黑屏，而且菜单里「V-Log 开关 / 回原厂」全都打不动（toggle 见 policy
#   就只走 restore，restore 又死在同一处）。
#   恢复指令（黑屏时 ssh 里跑）：
#     mkdir -p /mnt/mmc/NXDEMO51
#     nx_regtabwrite.arm dyn-restore        # 视频选择器回 24 条原厂地址
#     vlog.sh off                           # A18 回 a18_orig.bin + 清 policy
ensure_run() {
    [ -d "$RUN" ] || $BB mkdir -p "$RUN" 2>/dev/null
    [ -d /mnt/mmc/NXDEMO51 ] || $BB mkdir -p /mnt/mmc/NXDEMO51 2>/dev/null
}

# TC 曲线（tc.sh）与 V-Log 改的是同一条 gamma：TC 直接写 p7 gamma 对象的
# 19 节点表（SRC=0x8062f71c），V-Log 走 native 事务整条替换。叠加没有定论，
# 而且 V-Log 的 factory 基准会读到被 TC 改过的表。所以两边都做互斥。
tc_custom() {
    [ -x "$D/tc.sh" ] || return 1
    "$D/tc.sh" status 2>/dev/null | $BB grep -q '^CURVE=custom'
}

need_engine() {
    engine_ok && return 0
    say ENGINE "MISSING $ENGINE"
    pop "V-Log 引擎未安装" 3
    return 3
}

# 从 gamma-reload.sh 的输出块里摘 mode / phase
field() { printf '%s\n' "$1" | $BB sed -n "s/^$2=//p" | $BB head -n 1; }

do_curve() {   # $1 = toggle | log | restore
    need_engine || return 3
    ensure_run
    # 只挡「上曲线」这一侧；off 任何时候都放行 —— 黑屏时那是唯一的自救。
    marker=$RUN/.prepare-stamp
    [ -d "$RUN" ] && : > "$marker"
    case "$1" in
    toggle|log)
        if tc_custom; then
            say BLOCKED TC_CURVE_CUSTOM
            pop "先 TC 曲线回原厂（tc.sh off）" 4
            return 4
        fi ;;
    esac
    out=$("$ENGINE" "$1" 2>&1)
    rc=$?
    printf '%s\n' "$out"
    if [ "$rc" != 0 ]; then
        reason=$(field "$out" reason)
        # 真正的原因常常只在 clean-log-prepare 的日志里（gamma-reload.sh 只回
        # 一个原因码）。最常见的一条是挂了 LUT 时它拒绝：
        #   ERR View4 is not factory; disable other LUT mods and power-cycle first
        # 面板的状态行会显示我们 echo 出来的 `ERR ...` 那一行 —— 弹窗会被
        # 面板自己挡住，靠弹窗等于没提示。
        hint=
        if [ -r "$RUN/clean-log-prepare.log" ] && [ "$RUN/clean-log-prepare.log" -nt "$marker" ]; then
            hint=$($BB sed -n 's/^ERR //p' "$RUN/clean-log-prepare.log" | $BB head -n 1)
        fi
        case "$hint" in
        *"View4 is not factory"*)
            printf 'ERR 已挂 LUT：先 lut.sh off 再开 V-Log\n'
            pop "已挂 LUT，先 lut.sh off" 5
            return 4 ;;
        esac
        pop "V-Log 失败: ${reason:-rc=$rc}" 4
        return "$rc"
    fi
    # 回原厂之后再确认一次视频选择器：DYN24 全指 A18 是黑屏的直接原因，
    # 而 clean-log-release 里那一步有可能被跳过（半状态时 policy 已经没了）。
    if [ -f "$RUN/session/video_enabled" ]; then
        "$D/gamma/bin/nx_regtabwrite.arm" dyn-restore >/dev/null 2>&1 \
            && say VIDEO_POINTERS restored
    fi
    mode=$(field "$out" mode)
    case "$mode" in
    LOG)     pop "V-Log 已上曲线" 3 ;;
    FACTORY) pop "已回原厂 gamma" 3 ;;
    *)       pop "完成: ${mode:-UNKNOWN}" 3 ;;
    esac
    return 0
}

# 直接还原视频选择器（DYN24 的 24 条）。黑屏时 vlog.sh off 走不通的场合，
# 这一条能单独把画面救回来 —— 它不碰 gamma，也不碰 A18。
do_video_off() {
    vw=$D/gamma/bin/nx_regtabwrite.arm
    if [ ! -x "$vw" ]; then say VIDEO "MISSING $vw"; return 3; fi
    out=$("$vw" dyn-restore 2>&1)
    rc=$?
    printf '%s\n' "$out"
    if [ "$rc" = 0 ]; then pop "视频选择器已回原厂" 3; else pop "视频指针恢复失败" 4; fi
    return "$rc"
}

do_status() {
    if ! engine_ok; then
        say ENGINE "MISSING $ENGINE"
        return 3
    fi
    ensure_run
    "$ENGINE" status
}

do_gate() {
    if [ ! -x "$ARM" ]; then
        say GATE "MISSING $ARM"
        pop "vlog-arm.sh 缺失" 3
        return 3
    fi
    [ -d /tmp/nx-p0 ] || $BB mkdir -p /tmp/nx-p0 2>/dev/null
    out=$("$ARM" arm 2>&1)
    rc=$?
    printf '%s\n' "$out"
    # 明细落盘：弹窗只能塞十几个字，真出问题要看全七项
    printf '%s\n' "$out" > "$GATELOG" 2>/dev/null
    fails=$(printf '%s\n' "$out" | $BB grep -c '=FAIL')
    [ -n "$fails" ] || fails=0
    if [ "$rc" = 0 ]; then
        pop "V-Log 放行 通过" 3
    else
        pop "未放行: ${fails} 项失败" 4
    fi
    return "$rc"
}

case "${1:-}" in
toggle) do_curve toggle ;;
on)     do_curve log ;;
off)    do_curve restore ;;
status) do_status ;;
gate)   do_gate ;;
video-off) do_video_off ;;
*)
    echo 'usage: vlog.sh toggle|on|off|status|gate|video-off'
    echo "  ENGINE=$ENGINE"
    exit 2
    ;;
esac
