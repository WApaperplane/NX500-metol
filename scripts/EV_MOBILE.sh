#!/bin/bash
#=====================================================================
# EV_MOBILE.sh —— EV + WiFi → 直达 LUT 滤镜面板（方案 2b）
#=====================================================================
# 触发条件（keyscan 规则 A）：【按住 EV 键，再按 WiFi 键】
#   机型差异：WiFi 键 code 215 = nxkeyname "MOBILE"，NX500/NX1 通吃。
#
# ★ 2026-11-20 修正（用户明确要求）：**直接出 LUT 页**，不要中间的菜单页。
#   上一版从 `nxui.sh menu gui_evwifi.NX500` 进，是 2b 第一次落地时把
#   「单入口」理解成了「先给一页菜单」—— 那是多按一次，用户不要。
#   现在本槽位 = `nxui.sh lut`（面板 + `--tab lut`），滤镜页内：
#       WiFi 键 / AEL 键  → LUT ⇄ FilmLab 换层（进程内，不重启）
#       顶栏「混合」       → 双表混合（仅 LUT 层）
#       顶栏「菜单」       → 主菜单 gui_ini.NX500（实用功能 / V-Log 都在那儿）
#       顶栏「关闭」/ MENU / BACK / 半按快门 → 退出
#   同一槽位再按一次 = 关（nxui.sh 的 toggle 语义，保留）。
#
# ★ 历史：2026-10-18 本槽位就是直达 LUT（`nxui.sh lut`），中间被改成合并页，
#   现在回到直达 —— 但那时的 FilmLab 只能独占 EV+AEL，现在两层在同一面板里。
#   `gui_evwifi.NX500` 仍然保留（主菜单/脚本里可以引用），只是不再由按键进入。
#
# ★ 为什么用 nxui 而不是 mod_gui：
#   mod_gui 是三星原生 EFL 菜单，和 nxui 是两套渲染栈、两套焦点处理。
#   nxui 是我们自己的面板，菜单页与面板同一个二进制、同一套 .NX500 解析 ——
#   复用它是这套 mod 的核心优势，不该为了省事退回 EFL。
#=====================================================================

D=/opt/usr/nx-ks
BB=$D/busybox

if [ -x "$D/nxui.sh" ] && "$D/nxui.sh" available; then
    "$D/nxui.sh" lut
    exit $?
fi

# NXUI 不可用（未安装或已 disable）时给个提示，不再回退去起服务
if [ -x "$D/popup_timeout" ]; then
    nice -n +10 "$D/popup_timeout" " NXUI 未启用 " 3 >/dev/null 2>&1
fi
exit 1
