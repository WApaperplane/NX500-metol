#!/bin/bash
# ver.sh —— Metol 版本与血统（2026-11-20：改成一次显示完 + 关闭按钮）
#
# 原来是四条 popup 依次弹、每条 3 秒自己关 —— 看完要等 12 秒，还记不住。
# 现在走 nxdialog.sh：一个框里四行，点「关闭」或按任意退出键才收。
#
# 版本号先看机身那份：卡上的 /mnt/mmc/VERSION 常是上一个发行版留下的
# （2026-11-20 实测还是 2.88.0-nxwa.15.2），拿它当准会显示成"版本回退"。
D=/opt/usr/nx-ks

V=
for vf in "$D/VERSION" /mnt/mmc/VERSION; do
    [ -r "$vf" ] || continue
    V=$(awk '/^version[ \t]+/{print $2; exit}' "$vf" 2>/dev/null)
    [ -n "$V" ] && break
done
[ -n "$V" ] || V="1.15.6"
# /etc/version.info: 第 1 行固件版本（1.12），第 2 行机型（NX500）
FW=$(awk 'NR==1{print $1; exit}' /etc/version.info 2>/dev/null)
CAM=$(awk 'NR==2{print $1; exit}' /etc/version.info 2>/dev/null)
[ -n "$CAM" ] || CAM=NX500

if [ -x "$D/nxdialog.sh" ]; then
    exec "$D/nxdialog.sh" \
        "Metol $V" \
        "$CAM · FW $FW · DRIMe5" \
        "血缘 NX-KS 2.88 + 上游 modding" \
        "github.com/WApaperplane/metol"
fi

# 兜底：老机器没有 nxdialog.sh 就退回逐条弹窗
P="$D/popup_timeout"
[ -x "$P" ] || exit 0
for ln in "Metol $V" "$CAM · FW $FW · DRIMe5" "github.com/WApaperplane/metol"; do
    nice -n +10 "$P" " $ln " 3 >/dev/null 2>&1
done
exit 0
