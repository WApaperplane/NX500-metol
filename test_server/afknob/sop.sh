#!/bin/bash
# afknob/sop.sh —— AF 旋钮与降噪面的上机 SOP（PC 侧）
#
# 设计原则：默认只读。任何写入必须由人显式点名子命令，且带前置判据。
#
#   sh sop.sh find                 自动探测相机 IP（只探 22，ICMP 不可靠）
#   sh sop.sh probe                V1：只读 status + prefman + iqr（零写入）
#   sh sop.sh push                 把 nx_afknob.arm 推到相机并 chmod 755
#   sh sop.sh set <ng> <ok> <small>  显式写入（要求 status 先返回 STOCK）
#   sh sop.sh restore              写回 8/4/4
#   sh sop.sh smartrange           录像 Smart Range 裁决（get → set 1 → iqr 读回 → 复原）
#
# 环境变量：NXHOST 可跳过探测；NX_BIN 覆盖二进制路径
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=${NX_BIN:-$HERE/build/nx_afknob.arm}
BB=/opt/usr/nx-ks/busybox

say() { printf '%s\n' "$*"; }
die() { printf 'FATAL: %s\n' "$*" >&2; exit 1; }

nxs() { NXHOST="$HOST" bash "$ROOT/tools/nx-ssh.sh" "$@"; }

find_host() {
    local ip
    if [ -n "${NXHOST:-}" ]; then HOST=$NXHOST; return 0; fi
    for ip in 192.168.0.108 192.168.0.103 192.168.0.102 192.168.0.104 192.168.0.106; do
        if (exec 3<>/dev/tcp/$ip/22) 2>/dev/null; then
            exec 3<&- 3>&-
            HOST=$ip
            say "found $HOST"
            return 0
        fi
    done
    return 1
}

cmd_probe() {
    say "== [1/4] uptime / 相机进程 =="
    nxs cmd "$BB uptime; $BB pidof di-camera-app" || return 1

    say "== [2/4] V1: AF 三值（只读，0x8155044C/50/54）=="
    nxs cmd "$BB sh -c '
        [ -x /tmp/nx-afknob/nx_afknob.arm ] || { echo NEED_PUSH; exit 0; }
        /tmp/nx-afknob/nx_afknob.arm status
    '"

    say "== [3/4] prefman: 录像 Smart Range 槽（0xa380）=="
    nxs cmd "$BB prefman get 0 0xa380 l 2>&1 | $BB head -3"

    say "== [4/4] iqr: 降噪 / Smart Range / 锐度 现状 =="
    nxs cmd "$BB st cap iqr 2>/dev/null | $BB grep -aE 'HIGH_ISO_NR|LONG_TIME_NR|SMARTRANGE|PW_SHARPNESS'"
    say ""
    say "判读：V1 通过 = AFKNOB_VERDICT STOCK（三值 8/4/4）"
}

cmd_push() {
    [ -f "$BIN" ] || die "二进制不存在: $BIN（先跑 build_afknob.sh）"
    nxs cmd "$BB mkdir -p /tmp/nx-afknob" || return 1
    nxs push "$BIN" /tmp/nx-afknob/ || return 1
    nxs cmd "$BB chmod 755 /tmp/nx-afknob/nx_afknob.arm; $BB ls -l /tmp/nx-afknob/nx_afknob.arm; $BB md5sum /tmp/nx-afknob/nx_afknob.arm"
    say "本地 md5: $(md5sum "$BIN" | cut -d' ' -f1)"
}

cmd_set() {
    local ng=$1 ok=$2 small=$3
    say "== 前置：status 必须 STOCK，否则中止 =="
    local st
    st=$(nxs cmd "/tmp/nx-afknob/nx_afknob.arm status") || die "status 执行失败"
    printf '%s\n' "$st"
    printf '%s' "$st" | grep -q AFKNOB_VERDICT\ STOCK || die "当前不是 STOCK，拒绝写入（如需强制加 --force 手工执行）"
    say "== 写入 ng=$ng ok=$ok small=$small =="
    nxs cmd "/tmp/nx-afknob/nx_afknob.arm set $ng $ok $small"
    say "== 复核：重新 status（应显示 TUNED 且为新值）=="
    nxs cmd "/tmp/nx-afknob/nx_afknob.arm status"
}

cmd_restore() {
    nxs cmd "/tmp/nx-afknob/nx_afknob.arm restore"
    nxs cmd "/tmp/nx-afknob/nx_afknob.arm status"
}

cmd_smartrange() {
    say "== 录像 Smart Range 裁决 =="
    say "-- before: prefman 槽 --"
    nxs cmd "$BB prefman get 0 0xa380 l 2>&1 | $BB head -3"
    nxs cmd "$BB st cap iqr 2>/dev/null | $BB grep -a SMARTRANGE_MOVIE"
    say "-- 写入 1（ON）--"
    nxs cmd "$BB prefman set 0 0xa380 l 1 2>&1 | $BB head -2; $BB sync"
    nxs cmd "$BB sleep 1; $BB st cap iqr 2>/dev/null | $BB grep -a SMARTRANGE_MOVIE"
    say ""
    say "判读：iqr 从 eSMARTRANGE_OFF(0) 变 1 ⇒ 通道通，可做录像 DRO 选项"
    say "      仍是 0 ⇒ 写了不进 ISP，与 pw 七维同构，需要 App 侧确认动作"
    say "      复原：$BB prefman set 0 0xa380 l 0"
}

[ $# -ge 1 ] || { sed -n '2,16p' "$0"; exit 2; }
sub=$1; shift
case "$sub" in
    find)   find_host && say "NXHOST=$HOST" ;;
    probe)  find_host || die "没找到相机（22 全闭）。检查相机是否醒着"; cmd_probe ;;
    push)   find_host || die "没找到相机"; cmd_push ;;
    set)    [ $# -eq 3 ] || die "用法: set <ng> <ok> <small>"; find_host || die "没找到相机"; cmd_set "$1" "$2" "$3" ;;
    restore) find_host || die "没找到相机"; cmd_restore ;;
    smartrange) find_host || die "没找到相机"; cmd_smartrange ;;
    *) die "未知子命令: $sub" ;;
esac
