#!/bin/bash
#=====================================================================
# EV_AEL.sh —— EV + AEL：打开 V-Log 曲线页（2026-11-20）
#=====================================================================
# 为什么给这个键：V-Log 在菜单里的位置是「双击 EV → 主菜单 → 实用功能 →
# V-Log 曲线」，三层下去才够得着，而它是拍视频时最常切的开关之一。
# 这个槽位自从 11-20 把 FilmLab 并进 LUT 面板后就空着，正好用它。
#
#   EV + WiFi  → LUT 面板（LUT / FILMLAB 两个页签）
#   EV + AEL   → V-Log 曲线页
#
# 语义跟 EV+WiFi 一致：面板没开就开，已经在跑就关（nxui.sh 的 toggle）。
#
# ★ 只有这一种写法是对的：`nxui.sh vlog` = 「菜单模式 + 固定页
#   gui_vlog.NX500」，会跳过 gen_menu 与 filmlab 导出，起得快；直接跑
#   nxui.arm 会绕过 toggle，第二次按下去又起一个进程。
#
# ★ 历史（2026-11-20 上午）：这个槽位曾经 `exec EV_AEL.community.sh`，而
#   那是**黑屏录像器**（blackoutprep + killall onscreen_ks + 循环录像 10
#   分钟），按一下就整屏黑 —— 用户报的"黑屏"就是它，与 V-Log 无关。
#   脚本仍在机器上（/opt/usr/nx-ks/EV_AEL.community.sh），想用就手动 sh。
#   屏幕已经被弄黑的话：
#       killall -q blackout
#       nice -n +15 /opt/usr/nx-ks/onscreen_ks &
#=====================================================================
exec /opt/usr/nx-ks/nxui.sh vlog
