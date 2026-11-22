#!/bin/sh
# nxdialog.sh —— 多行提示（一个框显示完，右下角「关闭」）
#
# 与 nxpopup.sh 的分工：
#   nxpopup.sh  单行状态，3 秒自动消失（"已应用 xxx"这种）
#   nxdialog.sh 多行信息，用户点「关闭」或按任意退出键才收（版本、状态汇总）
#
# 用法: nxdialog.sh "第一行" "第二行" ...
# 面板在跑 → 写 /tmp/nxui-dialog（面板自己画在最上层，最多 8 行）
# 面板没跑 → 退回 popup_timeout，一行一个（老行为）
D=/opt/usr/nx-ks
BB=$D/busybox
[ $# -gt 0 ] || exit 0

if [ -x "$D/nxui/nxui.arm" ] && $BB pidof nxui.arm >/dev/null 2>&1; then
    : > /tmp/nxui-dialog || exit 0
    for ln in "$@"; do
        printf '%s\n' "$ln" >> /tmp/nxui-dialog || exit 0
    done
    exit 0
fi

for ln in "$@"; do
    [ -x "$D/popup_timeout" ] && nice -n +10 "$D/popup_timeout" " $ln " 3 >/dev/null 2>&1
done
exit 0
