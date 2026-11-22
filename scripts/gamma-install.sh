#!/bin/sh
# gamma-install.sh — 把 RC5 的 V-Log 曲线引擎整棵装进机身（幂等，带哈希验收）
#
# ── 用法 ──────────────────────────────────────────────────────────────
#   gamma-install.sh            安装；已就绪则什么都不做（幂等）
#   gamma-install.sh check      只校验目标树，不动任何文件
#   gamma-install.sh force      强制重拷（源更新过时用）
#
# 源  ：$GAMMA_SRC  默认 /mnt/mmc/nx-srwcap   （RC5 发布包解出来那棵，在 SD 上）
# 目标：$GAMMA_DST  默认 /opt/usr/nx-ks/gamma
#
# ── 为什么必须整棵搬 ──────────────────────────────────────────────────
# gamma-reload.sh 以 $D=脚本目录 为根，校验六个组件的 sha256：
#   OPS / GAMMA_RELOAD / IDLE / REFRESH / USB_GUARD / ENGINE(lut.sh)
# 任缺一个或被改过，它直接 fail 在身份检查，根本走不到功能。
# ⇒ 只拷 bin/nxl-gamma-reload.arm 是装不起来的。
#
# 这些二进制与脚本是 RC5 的发布产物，本脚本**不做任何修改**，只搬运 + 验收。
# 六个哈希在此硬编码：源被换过、被 CRLF 化、被 chmod 以外的工具动过，
# 都能当场发现，而不是等到上曲线时才发现身份不对。
#
# ── 为什么不用 cp 而用 tar ────────────────────────────────────────────
# SD 是 exFAT，没有可执行位；busybox cp -a 在源侧读到的模式全是 0666，
# 拷过来还得再补 chmod。tar 至少能把"源上是什么"如实带过来，再由本脚本
# 统一补位 —— 与 install.sh 末尾那段 chmod 兜底同一套做法。
#
# 退出码：0 就绪｜1 校验失败｜2 用法｜3 源缺失

BB=/opt/usr/nx-ks/busybox
KS=/opt/usr/nx-ks
SRC=${GAMMA_SRC:-/mnt/mmc/nx-srwcap}
DST=${GAMMA_DST:-$KS/gamma}
ENGINE=$DST/gamma-reload.sh

# 源与目标都必须满足的清单（RC5 release 的钉死值）
MANIFEST='c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d bin/nxl-fileops
5edf35e0ba839f198d18049e61f4b843d49507a59758e527a25204708dae9d6b bin/nxl-gamma-reload.arm
295fb39359b29739801fd69f71aaaa12c731efd1ee25d64c07c71c91fd94ffb3 bin/nxl-lutidle.arm
3a7b272e3be9ad81996c823f17f33d84e990a85eff0f252785db7d575a0ceac7 bin/nxl-native-refresh.arm
61bf59b3bf4ccba775525e40024d0a42b86cffd22a1b94d9a431a8102f266918 bin/nxl-usb-guard.arm
2d5c5d0c64242691413e41c8d4c833dbf864695e0d9c9c03a164337aff3345d2 lut.sh'

say() { printf '%s=%s\n' "$1" "$2"; }
die() {
    say RESULT "$1"
    exit "${2:-1}"
}

# $1 = 树根；逐个比对清单。全部命中返回 0，否则返回不匹配数（>0）。
verify() {
    _bad=0
    while read -r _want _path; do
        [ -n "$_want" ] || continue
        _got=
        if [ -f "$1/$_path" ]; then
            # 不能用 set -- 接 sha256sum：那会把函数的 $1（树根）冲掉
            _got=$($BB sha256sum "$1/$_path" 2>/dev/null | $BB cut -d' ' -f1)
        fi
        if [ "$_got" = "$_want" ]; then
            printf '  OK   %s\n' "$_path"
        else
            printf '  FAIL %s\n       want=%s\n       got=%s\n' \
                "$_path" "$_want" "${_got:-MISSING}"
            _bad=$((_bad + 1))
        fi
    done <<EOF
$MANIFEST
EOF
    return "$_bad"
}

MODE=${1:-install}
case "$MODE" in
install | force | check) ;;
*)
    echo 'usage: gamma-install.sh [check|force]'
    exit 2
    ;;
esac
say DST "$DST"
say ENGINE "$ENGINE"

# ---- check：只看目标 ----
if [ "$MODE" = check ]; then
    if [ ! -d "$DST" ]; then
        say TREE MISSING
        die NOT_INSTALLED 1
    fi
    verify "$DST" || die VERIFY_FAILED 1
    [ -f "$DST/data/clean-log/NXLOG_REF_V1.bin" ] || die NXLOG_REF_MISSING 1
    say RESULT OK
    exit 0
fi

# ---- 幂等：目标已就绪且不是 force，直接收工 ----
if [ "$MODE" != force ] && [ -d "$DST" ] && [ -f "$ENGINE" ]; then
    if verify "$DST" >/dev/null 2>&1 && [ -f "$DST/data/clean-log/NXLOG_REF_V1.bin" ]; then
        say VERIFY OK
        say ACTION none
        die ALREADY_INSTALLED 0
    fi
fi

# ---- 源必须完整：先在源侧验一遍，别把半棵树搬进去 ----
[ -d "$SRC" ] || die "SOURCE_MISSING $SRC" 3
[ -f "$SRC/gamma-reload.sh" ] || die "SOURCE_INCOMPLETE $SRC" 3
say SRC "$SRC"
say PHASE verify-source
verify "$SRC" || die SOURCE_VERIFY_FAILED 1
[ -f "$SRC/data/clean-log/NXLOG_REF_V1.bin" ] || die SOURCE_NXLOG_REF_MISSING 1

# ---- 搬：tar 保留源侧模式，缺的执行位稍后统一补 ----
say PHASE copy
$BB mkdir -p "$DST" || die MKDIR_FAILED 1
$BB tar cf - -C "$SRC" . 2>/dev/null | $BB tar xf - -C "$DST" 2>/dev/null || die COPY_FAILED 1

# ---- 补执行位 ----
# exFAT 源没有 x 位；tar 到 ext4 后可能是 0644。只对确实需要的补：
# 所有 .sh、bin/ 下全部、以及 data 里带 x 的（.bin 不用，但补了无害）。
for _f in "$DST"/*.sh "$DST"/bin/*; do
    [ -f "$_f" ] && chmod 755 "$_f" 2>/dev/null
done
unset _f

# ---- 目标侧验收：搬完再验一遍，确认没有截断 ----
say PHASE verify-target
verify "$DST" || die TARGET_VERIFY_FAILED 1
[ -f "$DST/data/clean-log/NXLOG_REF_V1.bin" ] || die TARGET_NXLOG_REF_MISSING 1

# ---- 冒烟：跑一次 status。它会附带跑完整套身份校验，
#      能过说明六个组件路径与哈希都对得上。
#      status 需要在 /tmp/nx-lut-lite（tmpfs），先补目录，
#      否则 gamma-reload.sh 会在 mkdir 之前就 exec，报 IDLE_RAM_REPORT_UNAVAILABLE。
say PHASE smoke
[ -d /tmp/nx-lut-lite ] || $BB mkdir -p /tmp/nx-lut-lite 2>/dev/null
_out=$("$ENGINE" status 2>&1)
_rc=$?
# ★ 判据不用 grep：本机 $BB = BusyBox 1.20.2，它的 grep 构建里没有 -x
#   （`grep [-HhnlLoqvsriwFE]`，实测 -qx 恒返回 1），而裸 grep 是
#   /bin/grep = GNU 2.5.1，-qx 正常。为了不依赖 PATH 解析，直接取末行比字符串。
_last=$(printf '%s\n' "$_out" | $BB tail -n 1)
if [ "$_last" = "end=NXL_GAMMA_RELOAD_END1" ]; then
    say SMOKE "ok rc=$_rc"
    printf '%s\n' "$_out" | $BB sed -n 's/^mode=/GAMMA_MODE=/p;s/^phase=/GAMMA_PHASE=/p;s/^reason=/GAMMA_REASON=/p'
else
    say SMOKE "fail rc=$_rc last=[$_last]"
    printf '%s\n' "$_out"
    die SMOKE_FAILED 1
fi

say ACTION installed
say RESULT OK
exit 0
