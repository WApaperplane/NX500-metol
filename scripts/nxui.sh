#!/bin/sh
# Independent Nuklear UI launcher. Installed binary/config live under nxui/.
#
# Startup cost here is dominated by process creation: on this camera a fork +
# exec of the dynamic busybox costs roughly 10-25 ms, and the old script spent
# a dozen of them before the panel could map. Everything a shell builtin can do
# is therefore done in-process:
#   grep '^NX500$' -> read loop      mkdir -p -> skipped when the dir exists
#   nice -n 15     -> nxui sets an absolute priority 19 itself
#   lut.sh list pipeline -> one lut.sh run plus builtins
D=/opt/usr/nx-ks
BB=$D/busybox
BIN=$D/nxui/nxui.arm
LAB=/mnt/mmc/filmlab
LOOKS=/mnt/mmc/looks
read NXUI_LAUNCH_UPTIME nxui_unused < /proc/uptime
export NXUI_LAUNCH_UPTIME

is_nx500() {
    while IFS= read -r line; do
        [ "$line" = NX500 ] && return 0
    done < /etc/version.info 2>/dev/null
    return 1
}
# ★ 2026-11-20：三个新入口
#   nxui.sh switch <mode>  从菜单里切到另一个面板（先让旧面板退净再起新的）
#   nxui.sh vlog           菜单模式 + V-Log 页（EV+AEL 用）
#   nxui.sh menu [page]    菜单模式 + 指定页（缺省 gui_ini.NX500）
SWITCH=
if [ "${1:-}" = switch ]; then
    SWITCH=1
    shift
    if [ $# -eq 0 ]; then
        echo 'nxui.sh switch menu|filmlab|lut|vlog [page]' >&2
        exit 2
    fi
fi
MODE=${1:-filmlab}
# 额外参数原样透传给 nxui.arm（例如 --inputlog /mnt/mmc/nxui/keys.log）。
# 必须先存成字符串：下面每一段都在用 `set --` 重建参数表，$@ 会被覆盖。
# 只支持不含空格的参数（键位采集路径不会带空格）。
[ $# -gt 0 ] && shift

# vlog 只是「菜单模式 + 固定页」的别名，不必再开一个 nxui 模式。
# PAGE 非空同时意味着「不要跑 gen_menu / filmlab 导出」——那些是给
# gui_ini 及其动态子页准备的，固定页不需要，白等几百毫秒。
PAGE=
case "$MODE" in
vlog)
    MODE=menu
    PAGE=gui_vlog.NX500
    ;;
menu)
    # nxui.sh menu <页>：只吃菜单文件名，别把 --inputlog 这类透传参数当页名
    if [ $# -gt 0 ]; then
        case "$1" in
        --*) : ;;
        *)
            PAGE=$1
            shift
            ;;
        esac
    fi
    ;;
esac
EXTRAS="$*"
case "$MODE" in
  disable)
    # ★ 2026-11-10 语义反转：默认可用，只有显式 disable 才关。
    #   旧语义是「必须 enable 才可用」，而 enable 只在开发者 SOP 里出现过，
    #   release 包从来没带过 enabled 标记 ⇒ 用户装完 nxui.sh available 恒假，
    #   EV+AEL / MENU 全部落到 mod_gui 的原版 popup。见 docs/current/NXUI_ENABLE_GATE_2026-11-10.md
    : > "$D/nxui/disabled"
    rm -f "$D/nxui/enabled"
    $BB killall -q -TERM nxui.arm 2>/dev/null
    exit 0 ;;
  enable)
    [ -x "$BIN" ] && is_nx500 || exit 1
    rm -f "$D/nxui/disabled" "$D/nxui/enabled"
    exit 0 ;;
  stop)
    $BB killall -q -TERM nxui.arm 2>/dev/null
    exit 0 ;;
  available)
    [ -x "$BIN" ] && [ ! -f "$D/nxui/disabled" ] && is_nx500
    exit $? ;;
  diag)
    echo "bin      : $([ -x "$BIN" ] && echo OK || echo "MISSING $BIN")"
    echo "model    : $(is_nx500 && echo NX500 || echo "OTHER ($($BB head -1 /etc/version.info 2>/dev/null))")"
    echo "disabled : $([ -f "$D/nxui/disabled" ] && echo yes || echo no)"
    echo "enabled  : $([ -f "$D/nxui/enabled" ] && echo 'yes (legacy flag, ignored)' || echo no)"
    echo "available: $( [ -x "$BIN" ] && [ ! -f "$D/nxui/disabled" ] && is_nx500 && echo yes || echo no )"
    exit 0 ;;
  menu|filmlab|lut|demo|list) ;;
  *)
    echo 'nxui.sh menu [页]|filmlab|lut|vlog|switch <mode>|demo|list|stop|enable|disable|available|diag [extra...]'
    echo '  extra 透传给 nxui.arm，例：nxui.sh lut --inputlog /mnt/mmc/nxui/keys.log'
    exit 2 ;;
esac
[ -x "$BIN" ] || exit 1
is_nx500 || exit 1
# ★ switch：菜单里点「FilmLab / LUT 滤镜」走这里。
#   不能直接调 `nxui.sh <mode>` —— 那一路是「再按一次就关」的 toggle，
#   而且 pidof 会命中还没退净的旧进程，结果就是「只杀不起」。
#   所以这里先发 TERM，再轮询等它真的消失，才起新的。
#   （nxui 收到 TERM 只清焦点+退窗，不等子进程；被杀的父进程不会等我们。）
if [ -n "$SWITCH" ]; then
    $BB killall -q -TERM nxui.arm 2>/dev/null
    _i=0
    while $BB pidof nxui.arm >/dev/null 2>&1 && [ "$_i" -lt 40 ]; do
        $BB usleep 50000 2>/dev/null || $BB sleep 1
        _i=$((_i + 1))
    done
    unset _i
elif $BB pidof nxui.arm >/dev/null 2>&1; then
    $BB killall -q -TERM nxui.arm
    exit 0
fi
# The FILMLAB table is shared by the menu and the strip. Hash once; unchanged
# recipes need no shell parser, rewrite to the SD card, or extra process.
# LUT mode reads its recipes from the look library instead, so it skips both
# the hash and the file read entirely.
if [ "$MODE" != lut ] && [ -z "$PAGE" ]; then
    [ -d "$LAB" ] || mkdir -p "$LAB"
    if [ -f "$LAB/recipes.json.nx-prev" ]; then
        [ -r "$D/nxstore.sh" ] || exit 1
        . "$D/nxstore.sh"
        nx_recover "$LAB/recipes.json" || exit 1
    fi
    SIG=$($BB cksum "$LAB/recipes.json") || exit 1
    OLD_SIG=
    [ ! -f /tmp/nxui-export.sig ] || read -r OLD_SIG < /tmp/nxui-export.sig
    if [ ! -s "$LAB/recipes.txt" ] || [ "$SIG" != "$OLD_SIG" ]; then
        "$D/filmlab.sh" export > /tmp/nxui-export.log 2>&1 || exit 1
        printf '%s\n' "$SIG" > /tmp/nxui-export.sig
    fi
fi
if [ "$MODE" = menu ] && [ -z "$PAGE" ]; then
    "$D/gen_menu.sh" >/dev/null 2>&1
    # Bitrate/ramp menus are generated only when entered, without global sync.
fi
$BB killall -q mod_gui 2>/dev/null
export DISPLAY=:0
export LC_ALL=C
# fontconfig rescans every font to rebuild its cache when the cache directory is
# not writable, and the stock camera config names no <cachedir> at all. That
# scan is 215-260 ms of a ~300 ms first frame. Point it at the card instead:
# measured 202 ms -> 25 ms on the second launch (qemu-arm, camera ABI).
[ -d /mnt/mmc/nxui/fc-cache ] || mkdir -p /mnt/mmc/nxui/fc-cache
[ -f "$D/nxui/fonts.conf" ] && export FONTCONFIG_FILE="$D/nxui/fonts.conf"
# 记住上次打开时停在哪一项（配方 slug / look 名，不是序号）。目录已由上面的
# fc-cache 建好；写失败只是下次从头开始，不影响面板。
#
# ★ 2026-11-13：这个 last-* 每次开关面板都重写同名文件，是 exFAT 重复目录项的
#   主要来源之一。改写到 tmpfs —— 反正它本来就是"下次从头开始也无所谓"的东西。
#   keys.conf 是用户配置、必须跨开机，所以只在启动时从 SD 读一次拷进来（读不
#   磨损卡）；运行中改 SD 上那份，重启后生效。
NXS=/tmp/nx/nxui
[ -d "$NXS" ] || $BB mkdir -p "$NXS" 2>/dev/null
if [ -f /mnt/mmc/nxui/keys.conf ] && [ ! -f "$NXS/keys.conf" ]; then
    $BB cp -f /mnt/mmc/nxui/keys.conf "$NXS/keys.conf" 2>/dev/null
fi
export NXUI_STATE_DIR=$NXS
# ★ 2026-11-20 方案 2b：两层滤镜装进同一个进程（LUT 层 + FilmLab 层），
#   菜单页里点「LUT / FilmLab」是进程内换层，不再 `nxui.sh switch`。
#   所以 LUT 列表必须**任何模式都准备**：菜单页随时可能切过去，只装一层
#   的话切过去就是空列表。生成失败不致命 —— 缺 lut.sh 时只是那一层空着，
#   不能让整个面板（包括 FilmLab）起不来。
LU=/tmp/nxui-look
if [ ! -s "$LU.rec" ] || [ "$LOOKS" -nt "$LU.stamp" ] || [ /mnt/mmc/lut/state/order -nt "$LU.stamp" ]; then
    if sh "$D/lut.sh" list > "$LU.names" 2>/dev/null; then
        # 9 字段配方格式：key|label|7 个占位值（LUT 不使用 PW 分量）。
        # Done with shell builtins: the old awk cost another process.
        : > "$LU.rec"
        while IFS= read -r name; do
            [ -n "$name" ] || continue
            printf '%s|%s|0|0|0|0|0|0|0\n' "$name" "$name" >> "$LU.rec"
        done < "$LU.names"
        : > "$LU.stamp"
    else
        echo 'nxui.sh: lut.sh list 失败，LUT 层将为空' >&2
    fi
fi
# vlog 域：同一层的第二张清单（/mnt/mmc/looks/_vlog，对数域输入的表）。
# 它不是第三个页签 —— 面板里「再点一次 LUT 页签」换到这份清单。
# 判据只看目录 mtime：_vlog 不进 order 文件，没有增删排序的额外信号；
# 目录不存在时 -nt 恒假，也不多花一次 stat。
LUV=/tmp/nxui-look-vlog
if [ ! -s "$LUV.rec" ] || [ "$LOOKS/_vlog" -nt "$LUV.stamp" ]; then
    if sh "$D/lut.sh" list --vlog > "$LUV.names" 2>/dev/null; then
        : > "$LUV.rec"
        while IFS= read -r name; do
            [ -n "$name" ] || continue
            printf '%s|%s|0|0|0|0|0|0|0\n' "$name" "$name" >> "$LUV.rec"
        done < "$LUV.names"
        : > "$LUV.stamp"
    else
        echo 'nxui.sh: lut.sh list --vlog 失败，V-Log 域将为空' >&2
    fi
fi
set -- --root "$D" --recipes "$LAB/recipes.txt" --engine "$D/filmlab.sh" \
       --lut-recipes "$LU.rec" --lut-engine "$D/lut.sh" \
       --lut-recipes-vlog "$LUV.rec"
if [ "$MODE" = menu ]; then set -- "$@" --menu "${PAGE:-gui_ini.NX500}"; fi
# lut / filmlab 只是「进来停在哪一层」的预设；两层永远都在。
if [ "$MODE" = lut ]; then set -- "$@" --tab lut; fi
if [ "$MODE" = filmlab ]; then set -- "$@" --tab pw; fi
if [ "$MODE" = demo ]; then set -- "$@" --dry-run; else set -- "$@" --live; fi
# 透传参数放在最后追加：上面所有 set -- 都重建整个参数表，提前塞进去会被冲掉。
if [ -n "$EXTRAS" ]; then
    for x in $EXTRAS; do set -- "$@" "$x"; done
fi
# Keep lifetime/kill handling in the UI: close waits for an in-flight apply.
# A separate session prevents telnet disconnect from terminating an apply halfway.
# ★ 不能写成 "$BB" setsid "$BB" "$BIN"：setsid 只 exec 一个 PROG，多套一层 BB
#   会让 busybox 把 nxui.arm 当 applet 名 → "applet not found"，面板静默不启动。
#   （旧版能跑是因为中间还有 `nice -n 15` 这层 applet；删掉 nice 后暴露。）
$BB setsid "$BIN" "$@" >> /tmp/nxui.log 2>&1 &
exit 0
