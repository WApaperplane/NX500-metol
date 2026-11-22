#!/bin/sh
# vlog-arm.sh — V-Log 前置放行：补齐 usb-guard 私有状态层，跑七项放行检查（幂等）
#
# ── 背景（2026-11-20 逆向定案，REA 反汇编 FUN_0002182c）────────────────────
# nxl-usb-guard.arm 判的不是「USB 插没插」，而是「sel_mode 这个 8 字节键的
# inode 身份未漂移」。它的私有状态层要求：
#
#   /opt/usr/nx-lut-lite/state              存在、uid=0、mode&022==0
#   /opt/usr/nx-lut-lite/state/usb-config   0700（键目录）
#   .../usb-config/guard.fd                 普通文件 0600，nlink=1，size=0，可 flock
#   .../usb-config/last-good.bin            80 字节基线快照（守卫自己建，勿手改）
#   .../usb-config/events.log               守卫自写的 CHECKPOINT / CHECK_FAILED 行
#
# RC5 从未装机 ⇒ /opt/usr/nx-lut-lite 整个不存在 ⇒ 守卫第一跳
# open("/opt/usr/nx-lut-lite/state") 失败，reason 停在**默认占位串**
# USB_CONFIG_PRIVATE_STATE_UNAVAILABLE（它在固件身份四连 sha256 全过之后才被
# 赋值，所以看到这个串反而说明身份检查全过了）。
#
# ⇒ 二进制不需要改。sha256 61bf59b3…266918 与 RC5 发布包一致，
#   gamma-reload.sh 的 USB_GUARD_SHA / OPS / GAMMA_RELOAD / IDLE / REFRESH
#   五道身份校验全部照旧成立。本脚本只补运行时状态，不动任何二进制。
#
# ── 用法 ──────────────────────────────────────────────────────────────────
#   vlog-arm.sh            等价 arm
#   vlog-arm.sh check      只读，只跑七项，不改任何状态
#   vlog-arm.sh arm        幂等补齐私有状态层，再跑七项（默认）
#   vlog-arm.sh repair     丢弃基线快照后重建（改过相机 USB 模式才需要）
#   vlog-arm.sh status     只报守卫自身状态，不跑放行检查
#
# 输出 KEY=VALUE；退出码 0=放行 1=未放行 2=用法 3=守卫缺失 4=preflight 缺失

BB=/opt/usr/nx-ks/busybox
KS=/opt/usr/nx-ks
CAP=/mnt/mmc/nx-srwcap
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd)

# 三处路径都可用环境变量覆盖，便于干跑与换机
PRIV=${VLOG_PRIV_ROOT:-/opt/usr/nx-lut-lite}
STATE=$PRIV/state
UCFG=$STATE/usb-config
GUARD=${VLOG_GUARD:-$CAP/bin/nxl-usb-guard.arm}
BASE=$UCFG/last-good.bin

say() { printf '%s=%s\n' "$1" "$2"; }
die() { say RESULT "$1"; exit "${2:-1}"; }

# ---- 幂等补齐私有状态层 ------------------------------------------------
# 只做三件事：建目录、修 mode、修 owner。绝不碰 last-good.bin / guard.fd 内容。
ensure_private_state() {
    if [ ! -d "$PRIV" ]; then
        mkdir -p "$PRIV" 2>/dev/null || return 1
        say ACTION "mkdir $PRIV"
    fi
    if [ ! -d "$STATE" ]; then
        mkdir -p "$STATE" 2>/dev/null || return 1
        say ACTION "mkdir $STATE"
    fi
    # 守卫要求 state 的 st_uid==0 且 (st_mode & 022)==0
    chown 0:0 "$STATE" 2>/dev/null
    chmod 700 "$STATE" 2>/dev/null
    # usb-config 由守卫自己 mkdirat(0700)；已存在但位不对时先修，否则守卫拒
    if [ -d "$UCFG" ]; then
        chown 0:0 "$UCFG" 2>/dev/null
        chmod 700 "$UCFG" 2>/dev/null
    fi
    [ -d "$STATE" ] || return 1
    say PRIV_STATE "$($BB stat -c '%a %u:%g' "$STATE" 2>/dev/null || echo UNKNOWN)"
    return 0
}

# ---- 跑守卫本身，只摘关键字段 ------------------------------------------
run_guard() {
    if [ ! -x "$GUARD" ]; then
        say GUARD "$GUARD"
        return 3
    fi
    out=$("$GUARD" preflight "${1:-VLOG_ARM}" 2>&1)
    grc=$?
    say GUARD_RC "$grc"
    say GUARD_REASON "$(printf '%s\n' "$out" | $BB sed -n 's/^reason=//p')"
    say GUARD_CHANGED "$(printf '%s\n' "$out" | $BB sed -n 's/^baseline_changed=//p')"
    say USB_PHYSICAL "$(printf '%s\n' "$out" | $BB sed -n 's/^usb_physical=//p')"
    return "$grc"
}

# ---- 跑七项放行检查（单一真源，不自带一份副本）--------------------------
run_preflight() {
    pf=
    for c in "$KS/preflight.sh" "$CAP/preflight.sh" "$SELF/preflight.sh"; do
        [ -f "$c" ] && { pf=$c; break; }
    done
    if [ -z "$pf" ]; then
        say PREFLIGHT MISSING
        return 4
    fi
    "$BB" sh "$pf" vlog
}

MODE=${1:-arm}
case "$MODE" in
check | arm | repair | status) ;;
*)
    echo "usage: vlog-arm.sh [check|arm|repair|status]"
    exit 2
    ;;
esac

case "$MODE" in
check)
    # 纯只读：不加目录、不改 mode、不动基线
    run_preflight
    exit $?
    ;;
status)
    run_guard VLOG_STATUS
    exit $?
    ;;
arm)
    ensure_private_state || die PRIVATE_STATE_CREATE_FAILED 1
    run_guard VLOG_ARM
    grc=$?
    [ "$grc" = 3 ] && die GUARD_MISSING 3
    run_preflight
    exit $?
    ;;
repair)
    # 只有当 sel_mode 被改过（内容不再是 0x29）导致守卫拒载时才需要。
    # 守卫会用自己的 mkdirat/创建逻辑把 guard.fd 与 last-good.bin 重建。
    if [ -f "$BASE" ]; then
        rm -f "$BASE" && say ACTION "rm $BASE" || die BASELINE_DROP_FAILED 1
    else
        say ACTION "baseline absent"
    fi
    ensure_private_state || die PRIVATE_STATE_CREATE_FAILED 1
    run_guard VLOG_REPAIR
    grc=$?
    [ "$grc" = 3 ] && die GUARD_MISSING 3
    run_preflight
    exit $?
    ;;
esac
