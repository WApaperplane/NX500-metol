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
showmetheway(){
	# IP 只显示一次(4 秒后消失)。常驻查看走主菜单: 每次按 EV 键开菜单时
	# gen_menu.sh 会把当前 IP / Telnet 状态写进菜单标签, 不再每 4 秒弹窗刷屏。
	IP=`ip addr ls|grep inet|grep mlan0|cut -d/ -f 1|grep -o '[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}'`
	[[ -z $IP ]] && IP="Enable WiFi"
	killall onscreen_rc
	 nice -n +10 /opt/usr/nx-ks/onscreen_rc "rc: $IP" &
	sleep 4
	killall onscreen_rc
}
showmetheway&
#tothenextwhiskeybar
