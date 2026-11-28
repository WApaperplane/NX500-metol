#!/bin/bash
#
killall -q nx-input-injector
killall -q xev-nx
killall -q nx-remote-controller-daemon
killall -q onscreen_rc
#
export APP_PATH=/opt/usr/nx-ks/nx-rc
# --- nx-rc 附属服务: 8080 httpd(缩略图 CGI / 参数 API / WiFi push 接收) ---
# push-install: 部署 push-cgi 到 capdtm cgi-bin(幂等); thumb-install: 部署
# thumb-cgi 并确保 8080 httpd 启动(带 pid 防重复)。Web 遥控一开即全套可用。
[ -x "$APP_PATH/push/push-install.sh" ]   && "$APP_PATH/push/push-install.sh"   >/dev/null 2>&1
[ -x "$APP_PATH/thumb/thumb-install.sh" ] && "$APP_PATH/thumb/thumb-install.sh" start >/dev/null 2>&1
# --- 缩略图预热:2026-11-22 起默认不跑 ---
# 实机结论：NX500 原片自带 320px(IFD1) / 1616px(MPF) 内嵌图，thumb-cgi 直切
# 字节即可；预热 worker 跑 62 张的实际结果是 generated=0 sliced=62 —— 纯空转，
# 还白占单核 CPU 与 SD 读。
# 需要预热时（导入外机照片 / PNG）手动跑：
#   /opt/usr/nx-ks/nx-rc/thumb/thumb-prewarm.sh start 320 75 300 1024 85 60
# 原自动调用保留在此备查：
# [ -x "$APP_PATH/thumb/thumb-prewarm.sh" ] && "$APP_PATH/thumb/thumb-prewarm.sh" start 320 75 300 1024 85 60 >/dev/null 2>&1 &
#
nice --adjustment=19 $APP_PATH/nx-remote-controller-daemon &> /dev/null &
#
# ★ 2026-11-28 删除开机那 4 秒 IP 条（onscreen_rc "rc: <IP>"）及其函数。
#   它是「远程控制」checkbox 被写进 auto/ 后每次开机自动跑出来的那个 IP 弹窗；
#   地址在主菜单的「IP: x.x.x.x [SSH开]」那一行随时可看，不需要开机打扰。
#   需要临时看连接串：菜单点那一行（ssh_hint.sh --ui）。
#tothenextwhiskeybar
