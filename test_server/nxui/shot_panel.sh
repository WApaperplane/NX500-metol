#!/bin/bash
# shot_panel.sh -- 在开发机（WSL2 + WSLg）上把面板渲染一帧，用来肉眼验版式。
#
# 用途：改完 nxui.c / 菜单文件后，不必上机就能看到 720x292 的真实排版，
#       检查英文长标签会不会被截断（3 列网格每格 133 px，英文比中文长）。
#
# 依赖：qemu-arm-static + 相机 rootfs（默认 /opt/nxks2/rootfs-112）+ WSLg 的 X。
# 用法：bash test_server/nxui/shot_panel.sh [en|zh] [菜单文件] [输出 ppm]
#   例：bash test_server/nxui/shot_panel.sh en scripts/gui_ini.NX500 /tmp/en.ppm
#
# ★ 环境变量必须 LC_ALL=C：相机 glibc 2.13 的 locale 数据在 qemu 下遇到
#   C.UTF-8 会直接 assert 崩溃（不是面板的问题，上机不会出现）。
set -u
LANG_SEL=${1:-en}
MENU=${2:-gui_ini.NX500}
OUT=${3:-/tmp/nxui_shot.ppm}
RF=${NXUI_ROOTFS:-/opt/nxks2/rootfs-112}

cd "$(dirname "$0")/../.." || exit 1
export LC_ALL=C
export DISPLAY="${DISPLAY:-:0}"

BIN=scripts/nxui/nxui.arm
[ "$LANG_SEL" = en ] && BIN=scripts/nxui/nxui_en.arm
[ -x "$BIN" ] || { echo "缺 $BIN"; exit 1; }

# 菜单按 --root 解析；直接把 root 指到 scripts/，与机上 /opt/usr/nx-ks 同构。
# --font 给主机装了的字体：机上那份 NXUI_CJK 在开发机通常没有。
FONT=${NXUI_SHOT_FONT:-$(fc-list 2>/dev/null | head -1 | cut -d: -f2)}
[ -n "$FONT" ] || FONT="DejaVu Sans"

rm -f "$OUT"
timeout 60 qemu-arm-static -L "$RF" "$BIN" \
    --root "$PWD/scripts" \
    --recipes /tmp/nxui-shot-rec.txt --engine /opt/usr/nx-ks/filmlab.sh \
    --lut-recipes /tmp/nxui-shot-rec.txt --lut-engine /opt/usr/nx-ks/lut.sh \
    --lut-recipes-vlog /tmp/nxui-shot-rec.txt \
    --menu "$MENU" --snapshot "$OUT" --font "$FONT"
rc=$?
echo "rc=$rc  ->  $OUT"
exit $rc
