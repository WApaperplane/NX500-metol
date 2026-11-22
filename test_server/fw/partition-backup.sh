#!/bin/bash
# @gate script=partition-backup.sh opens=ro one_shot=1 gate=none pconly=1
# NX500 全分区备份（只读 dd → SD → 取回 PC，双侧 md5）
#
# 背景：2026-10-11 那份 emmcbak 备份如今只剩 p12_pristine.bin，其余 10 个文件已丢失；
#       而 p1/p2/p3/p5 不在 SLP 固件包内，一旦损坏没有替代来源。
#       本脚本用于重建这份备份，并强制三副本。
#
# 用法：
#   bash test_server/fw/partition-backup.sh plan     只列计划与空间估算（不连相机）
#   bash test_server/fw/partition-backup.sh run      连相机执行（只读 dd，写 SD）
#   bash test_server/fw/partition-backup.sh pull     把 SD 上的备份取回 PC 并核 md5
#
# 环境变量：NXHOST 指定相机 IP；DEST 指定本地落地目录（默认 E:/NXWA/_deliverables/fwbackup）
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
DEST=${DEST:-/e/NXWA/_deliverables/fwbackup}
REMOTE=/mnt/mmc/fwbackup
BB=/opt/usr/nx-ks/busybox

# 分区 → 大小(MiB)。p10/p14 默认不备（SLP 覆盖 / 可重建），--all 才备。
declare -A PART=(
    [boot0]=4 [boot1]=4
    [p1]=20 [p2]=10 [p3]=30 [p5]=20 [p6]=10 [p7]=30 [p8]=50
    [p9]=150 [p11]=100 [p12]=5 [p13]=10
)
declare -A OPT=( [p10]=1024 [p14]=2300 )
ORDER="boot0 boot1 p1 p2 p3 p5 p6 p7 p8 p9 p11 p12 p13"

size_of() {
    local k=$1
    if [ -n "${PART[$k]:-}" ]; then echo "${PART[$k]}"; else echo "${OPT[$k]:-0}"; fi
}

total() {
    local bytes=0 k
    for k in $ORDER; do bytes=$((bytes + $(size_of $k))); done
    if [ "$ALL" = "1" ]; then
        for k in "${!OPT[@]}"; do bytes=$((bytes + OPT[$k])); done
    fi
    echo "$bytes"
}

nxs() { NXHOST="$HOST" bash "$ROOT/tools/nx-ssh.sh" "$@"; }

find_host() {
    if [ -n "${NXHOST:-}" ]; then HOST=$NXHOST; return 0; fi
    for ip in 192.168.0.108 192.168.0.103 192.168.0.102 192.168.0.104; do
        if (exec 3<>/dev/tcp/$ip/22) 2>/dev/null; then
            exec 3<&- 3>&-
            HOST=$ip; echo "found $HOST"; return 0
        fi
    done
    return 1
}

ALL=0
[ "${1:-}" = "--all" ] && { ALL=1; shift; }
[ "$ALL" = "1" ] && ORDER="$ORDER p10 p14"
case "${1:-plan}" in
plan)
    echo "计划备份（默认 13 个分区，不含 p10/p14）："
    local_bytes=0
    for k in $ORDER; do
        sz=$(size_of "$k")
        printf '  %-6s %5d MiB\n' "$k" "$sz"
        local_bytes=$((local_bytes + sz))
    done
    echo "合计 ${local_bytes} MiB（约 $(awk "BEGIN{printf \"%.1f\", $local_bytes/1024}") GiB）"
    echo "可选（--all）：p10=1024 MiB（SLP 已覆盖）、p14=2300 MiB（mod 本体，可重建）"
    echo "落地：SD $REMOTE → 本地 $DEST"
    echo
    echo "★ 需要 SD 余量 ≥ $(( local_bytes / 1024 + 2 )) GiB（含余量）"
    ;;
run)
    find_host || { echo "FATAL: 没找到相机（22 全闭）" >&2; exit 1; }
    

    echo "== 环境 =="
    nxs cmd "$BB uptime; $BB df -k /mnt/mmc | $BB tail -1" || exit 1

    echo "== 建目录 =="
    nxs cmd "$BB mkdir -p $REMOTE; $BB sync" || exit 1

    echo "== 逐分区 dd（只读源，写 SD）=="
    for k in $ORDER; do
        node=/dev/mmcblk0$k
        printf '%-6s <- %s ... ' "$k" "$node"
        nxs cmd "$BB dd if=$node of=$REMOTE/$k.bin bs=1M 2>&1 | $BB tail -1" || echo "FAIL"
    done

    echo "== sync + 生成清单 =="
    nxs cmd "$BB sync; $BB sleep 2; cd $REMOTE && $BB md5sum *.bin > MANIFEST.md5; $BB ls -l *.bin; echo ---; $BB cat MANIFEST.md5"
    ;;
pull)
    find_host || { echo "FATAL: 没找到相机" >&2; exit 1; }
    mkdir -p "$DEST"
    echo "== 取回 =="
    nxs pull "$REMOTE" "$DEST" || exit 1
    echo "== 双侧核对 =="
    ( cd "$DEST/fwbackup" 2>/dev/null || cd "$DEST"; md5sum -c MANIFEST.md5 )
    echo "== 与出厂基线对照 =="
    "$0" check "$DEST/fwbackup" 2>/dev/null || "$0" check "$DEST"
    echo "★ 别忘了第二、第三副本（另一块盘 / 网盘）。单副本等于没有。"
    ;;
check)
    BASE=$ROOT/raw8/emmcbak/BASELINE.md5
    [ -f "$BASE" ] || { echo "缺基线文件 $BASE" >&2; exit 1; }
    DIR=${2:-$DEST}
    cd "$DIR" 2>/dev/null || { echo "目录不存在: $DIR" >&2; exit 1; }
    echo "对照基线: $BASE"
    echo "（口径：整分区 dd。p7 整分区 md5 ≠ SLP 前 12,075,648 B 的 md5，两者都对）"
    while read -r md5 name; do
        case "$md5" in \#*|"") continue ;; esac
        if [ ! -f "$name" ]; then
            printf '%-5s %-18s 未采集\n' "SKIP" "$name"; continue
        fi
        got=$(md5sum "$name" | cut -d' ' -f1)
        if [ "$got" = "$md5" ]; then
            printf '%-5s %-18s %s\n' "OK" "$name" "$got"
        elif [ "$name" = "p12.bin" ] && [ "$got" = "4b34d5e89d81ae3d9abeb1ceb4923a18" ]; then
            printf '%-5s %-18s %s（纯净态 0x00，比补丁基线更好）\n' "OK" "$name" "$got"
        else
            printf '%-5s %-18s 基线 %s 实得 %s\n' "DIFF" "$name" "${md5:0:12}" "${got:0:12}"
        fi
    done < "$BASE"
    echo "判读：OK = 与 2026-10-11 出厂态一致；DIFF = 该分区被改过，或采集口径不同（先看文件大小）"
    ;;
*)
    sed -n '2,20p' "$0"; exit 2 ;;
esac
