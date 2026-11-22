#!/bin/sh
# preflight.sh — V-Log / SRW 的前置条件检查（只读，不改任何状态）
#
# 用法：
#   sh preflight.sh vlog      # V-Log 曲线前置
#   sh preflight.sh srw       # SRW 重显影前置
#
# 输出 KEY=VALUE 行，最后 READY=0/1。任一项 FAIL 即 READY=0。
#
# 前置条件分两层，不能混：
#   资源层  A18 槽（0x81119F00）必须回原厂 —— LUT 与 SRW/V-Log 共用同一槽
#   效果层  PW（prefman 标量）必须中性 —— PW 曲线与 gamma 曲线叠加后无法归因
# FilmLab 走 prefman（slot 9-12），LUT 走 A18，两者资源上不冲突；
# 但效果上都会改 ISP tone curve，所以跑 V-Log/SRW 前仍要把 PW 归中性。

BB=/opt/usr/nx-ks/busybox
KS=/opt/usr/nx-ks
CAP=/mnt/mmc/nx-srwcap
MODE=${1:-}

PW_NEUTRAL="100 100 100 10 10 10 10"
SRW_CAP_KB=$((176 * 1024))    # 176 MiB

say() { printf '%s=%s\n' "$1" "$2"; }
fail() { FAILED=$((FAILED + 1)); say "$1" "FAIL:$2"; }
pass() { say "$1" "PASS"; }

FAILED=0
case "$MODE" in
vlog | srw) ;;
*)
    echo "usage: preflight.sh vlog|srw"
    exit 64
    ;;
esac
say MODE "$MODE"

# ---- 1. 不在录像 / 收尾 ----
# p0-state.sh 装机后在 $KS，调试期在 /tmp/nx-p0（/tmp 掉电即失，不能只认它）
P0STATE=
for c in "$KS/p0-state.sh" /tmp/nx-p0/p0-state.sh; do
    [ -x "$c" ] && { P0STATE=$c; break; }
done
if [ -n "$P0STATE" ]; then
    st=$($BB sh "$P0STATE" 2>/dev/null)
    state=$(printf '%s\n' "$st" | sed -n 's/^STATE_TRUE=//p')
    say REC_STATE "${state:-UNKNOWN}"
    if [ "$state" = IDLE ]; then
        pass NOT_RECORDING
    else
        fail NOT_RECORDING "${state:-UNKNOWN}（IDLE 只表示无 mp4 fd，不等于非收尾）"
    fi
else
    fail NOT_RECORDING "p0-state.sh 缺失（应在 $KS/p0-state.sh）"
fi

# ---- 2. A18 回原厂（资源层）----
lst=$($KS/lut.sh status 2>/dev/null)
say LUT_STATUS "$(printf '%s\n' "$lst" | head -1)"
if printf '%s\n' "$lst" | grep -q '^IDLE'; then
    pass LUT_IDLE
else
    fail LUT_IDLE "LUT 不是 IDLE 状态"
fi
if printf '%s\n' "$lst" | grep -q 'BACKUP absent'; then
    pass A18_NO_BACKUP
else
    # 有备份说明 begin 跑过，A18 可能被改
    fail A18_NO_BACKUP "存在备份，A18 可能非原厂"
fi
if printf '%s\n' "$lst" | grep -q 'STOCK  present'; then
    pass A18_STOCK_PRESENT
else
    fail A18_STOCK_PRESENT "缺原厂表"
fi

# ---- 3. PW 中性（效果层）----
chk=$($KS/filmlab.sh check 2>/dev/null)
line=$(printf '%s\n' "$chk" | grep 'ISP 现用参数')
if [ -n "$line" ]; then
    cur=$(printf '%s\n' "$line" | tr -cs '0-9' ' ' | awk '{print $1,$2,$3,$4,$5,$6,$7}')
    say PW_CURRENT "$cur"
    if [ "$cur" = "$PW_NEUTRAL" ]; then
        pass PW_NEUTRAL
    else
        fail PW_NEUTRAL "当前「$cur」≠ 中性「$PW_NEUTRAL」，先 filmlab.sh neutral"
    fi
else
    fail PW_NEUTRAL "读不到 ISP 现用参数"
fi

# ---- 4. 各自特有项 ----
case "$MODE" in
vlog)
    if [ -x "$CAP/bin/nxl-usb-guard.arm" ]; then
        out=$("$CAP/bin/nxl-usb-guard.arm" preflight BEFORE_GAMMA 2>&1)
        rc=$?
        # 只摘 rc + reason；完整字段由 vlog-arm.sh status 展开，避免这里刷十几行
        say USB_GUARD "rc=$rc reason=$(printf '%s\n' "$out" | $BB sed -n 's/^reason=//p')"
        if [ "$rc" = 0 ]; then pass USB_CLEAR; else fail USB_CLEAR "$out"; fi
    else
        fail USB_CLEAR "usb-guard 缺失"
    fi
    # 曲线引擎必须已装进机身（gamma-install.sh）。只查 SD 上那份不算数：
    # 从菜单点「V-Log 开关」时跑的是 $KS/gamma/ 这一棵。
    if [ -x "$KS/gamma/gamma-reload.sh" ]; then
        pass GAMMA_ENGINE
    else
        fail GAMMA_ENGINE "未安装，跑 gamma-install.sh"
    fi
    for f in bin/nxl-gamma-reload.arm bin/nxl-lutidle.arm bin/nxl-native-refresh.arm \
        lut.sh lut-core.sh gamma-reload.sh data/clean-log/NXLOG_REF_V1.bin; do
        [ -e "$CAP/$f" ] || fail CAP_FILES "缺 $f"
    done
    [ "$FAILED" = 0 ] && pass CAP_FILES
    ;;
srw)
    # 注意：SRW 工作区来自 CAP 池，不是 Linux 的 MemFree+Cached。
    # 必须读 st cap capmm show 的 Cur Allocable，两个池子不能混用。
    avail=$(st cap capmm show 2>/dev/null | $BB grep -a "Cur Allocable"         | $BB awk '{for(i=1;i<=NF;i++) if($i ~ /^[0-9]+$/ && $i+0 > 1000000) {print $i; exit}}')
    avail_kb=$(( ${avail:-0} / 1024 ))
    say CAP_ALLOC_KB "$avail_kb"
    if [ "${avail_kb:-0}" -ge "$SRW_CAP_KB" ]; then
        pass CAP_ENOUGH
    else
        fail CAP_ENOUGH "$avail_kb < $SRW_CAP_KB KB"
    fi
    ;;
esac

if [ "$FAILED" = 0 ]; then
    say READY 1
    exit 0
fi
say READY 0
say FAILED_COUNT "$FAILED"
exit 1
