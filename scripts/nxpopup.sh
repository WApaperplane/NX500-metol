#!/bin/sh
# nxpopup.sh —— 面板内横幅（替代 popup_timeout，2026-11-20）
#
# 为什么要有它：popup_timeout 是独立的 EFL 窗口，会被 nxui 这块
# override-redirect 面板盖住 —— 菜单里点一项、脚本弹提示，用户什么都看不到，
# 表现就是"点了没反应"。改成让面板自己画：文字/字号/配色都跟当前 UI 一致，
# 而且一定在最上层。
#
# 用法: nxpopup.sh "<文字>" [秒]
#   面板在跑  → 写 /tmp/nxui-banner（面板 4 Hz 读走并画），本脚本按秒数阻塞，
#               保持"上一条看完再弹下一条"的原有节奏；
#   面板没跑  → 退回 popup_timeout（独立窗口，至少还能看见）。
D=/opt/usr/nx-ks
BB=$D/busybox
TXT=${1:-}
SEC=${2:-3}
[ -n "$TXT" ] || exit 0

if [ -x "$D/nxui/nxui.arm" ] && $BB pidof nxui.arm >/dev/null 2>&1; then
    printf '%s\t%s\n' "$TXT" "$SEC" > /tmp/nxui-banner 2>/dev/null || exit 0
    $BB sleep "$SEC" 2>/dev/null
    exit 0
fi

[ -x "$D/popup_timeout" ] && nice -n +10 "$D/popup_timeout" " $TXT " "$SEC" >/dev/null 2>&1
exit 0
