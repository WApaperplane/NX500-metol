#!/bin/bash
# 双击 EV：优先 nxui 面板；面板起不来就退回经典 popup（mod_gui）。
#
# ★ 2026-11-27 加回退：1.15.8/1.15.9 的英文包把「面板启动失败」变成了
#   「整机没有菜单」——启动器传的新参数老二进制不认识，面板打一行 Usage
#   就 return 2，而 nxui.sh 是后台起进程、照样 exit 0，于是双击 EV 看起来
#   像没反应。卸载入口只在菜单里，用户当场被锁死。
#   现在：面板 1 秒内没露面就自动拉起经典 popup，至少菜单（含卸载）还在。
classic() {
    [[ $(echo $(st cap capdtm getusr MONITOROUT) | grep LCD) > ""  ]] || { $(st app bb lcd on; st app disp lcd) &&  sleep 1 ; } &
    killall -q mod_gui
    nice -n -15 /opt/usr/nx-ks/loadgui.sh&
    killall -q popup_entry & killall -q popup_ok &  killall -q focus_stack & killall -q focus_buttons & killall -q onscreen_ov &  killall -q onscreen_235 &
}

BB=/opt/usr/nx-ks/busybox
if [ -x /opt/usr/nx-ks/nxui.sh ] && /opt/usr/nx-ks/nxui.sh available; then
    # 先记下本来有没有面板：有 ⇒ 这一次是「再按一次关掉」，关掉后不该再弹菜单。
    was=$($BB pidof nxui.arm 2>/dev/null)
    /opt/usr/nx-ks/nxui.sh menu
    rc=$?
    [ -n "$was" ] && exit $rc
    if [ $rc -ne 0 ]; then
        echo "EV_EV: nxui.sh rc=$rc，回退经典菜单" >> /tmp/nxui.log
        classic
        exit $rc
    fi
    # 后台等：成功时第一次 pidof 就命中，等于零成本；失败时才走兜底，
    # 而且不拖住按键线程（keyscan 是同步调用本脚本的）。
    #
    # ★ 命中 ≠ 起来（2026-11-28 补）：老二进制打一行 Usage 就 return 2，
    #   进程在 pidof 抓到的那一瞬之后立刻消失，原来是「一命中就 exit 0」，
    #   于是既不报错也不回退。现在命中后还要过 400 ms 存活窗口，
    #   且 pid 必须始终是同一个（换了 pid = 崩过又被拉起，同样算失败）。
    #   仍未覆盖的一种：进程活着但窗口没显示——那需要在 nxui.arm 里
    #   XMapWindow+flush 之后回写一个 ready 文件，属面板侧改动，未做。
    (
        # 等待原语要探测：$BB usleep 正常一直在，但若它缺失、`sleep 0.05` 又不支持
        # 小数（busybox 常见），两者都会立刻返回 ⇒ 20 次循环变忙等，存活窗口退化成 0，
        # 兜底自己就失效了。所以先测一次，选一个真的会等的。
        if $BB usleep 1000 2>/dev/null; then w() { $BB usleep 50000; }
        elif sleep 0.05 2>/dev/null; then w() { sleep 0.05; }
        else w() { sleep 1; }; fi
        i=0
        seen=
        while [ $i -lt 20 ]; do
            p=$($BB pidof nxui.arm 2>/dev/null)
            if [ -n "$p" ]; then seen=${p%% *}; break; fi
            w
            i=$((i + 1))
        done
        if [ -z "$seen" ]; then
            echo "EV_EV: 面板 1 s 内未启动，回退经典菜单（面板日志见 /tmp/nxui.log）" >> /tmp/nxui.log
            classic
            exit 0
        fi
        i=0
        while [ $i -lt 8 ]; do
            w
            c=$($BB pidof nxui.arm 2>/dev/null)
            if [ -z "$c" ] || [ "${c%% *}" != "$seen" ]; then
                echo "EV_EV: 面板启动后 400 ms 内退出或重启（pid $seen → ${c:-none}），回退经典菜单" >> /tmp/nxui.log
                classic
                exit 0
            fi
            i=$((i + 1))
        done
    ) &
    exit $rc
fi
classic
