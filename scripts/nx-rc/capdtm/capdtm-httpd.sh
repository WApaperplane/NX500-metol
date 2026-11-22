#!/bin/sh
# ============================================================
# capdtm-httpd - 启动/停止 busybox httpd(8080)提供拍摄参数 API
#
# 2026-11-22 修掉两个稳定性缺陷：
#   1. stop 原先是 `$BUSYBOX killall busybox`，会连带杀掉所有 comm=busybox
#      的进程（telnetd 等），而紧接着才是精确的按 `busybox httpd` 过滤。
#      前一句多余且自杀，已删。
#   2. 原先不带 -f 启动：busybox httpd 默认 daemonize，`$!` 指向立刻退出的
#      父进程 ⇒ pidfile 永远无效 ⇒ `kill -0` 恒失败 ⇒ 每次 start 都再 fork
#      一个注定绑定失败的 httpd。改用 -f 让 $! 就是服务进程本身。
#
# 用法: capdtm-httpd.sh start|stop|status
# 建议加入 NX-KS 开机脚本 auto/a_init.sh 末尾:
#   /opt/usr/nx-ks/nx-rc/capdtm/capdtm-httpd.sh start &
# ============================================================

BASE=/opt/usr/nx-ks/nx-rc/capdtm
PORT=8080
# busybox 不在系统 PATH 里(实测 command not found),必须用绝对路径
BUSYBOX=/opt/usr/nx-ks/busybox
WWW=$BASE/www
PIDFILE=/tmp/capdtm_httpd.pid
LOCKFILE=/tmp/capdtm_httpd.lock

# 串行化 start/stop：否则两个并发 start 会同时通过判活、各起一个 httpd。
CONTROL_LOCKED=0
control_lock() {
    [ "$CONTROL_LOCKED" = 1 ] && return 0
    [ -e "$LOCKFILE" ] || : > "$LOCKFILE" 2>/dev/null || return 1
    exec 9>"$LOCKFILE" || return 1
    _i=0
    while ! "$BUSYBOX" flock -n 9; do
        _i=$((_i + 1))
        [ "$_i" -le 20 ] || return 1
        "$BUSYBOX" usleep 100000 2>/dev/null || sleep 1
    done
    CONTROL_LOCKED=1
    return 0
}

# 判活：pidfile 里的进程必须仍是「这个根目录、这个端口」的 httpd。
# 只看 kill -0 不够 —— PID 会复用，误杀无关进程后果更糟。
alive() {
    [ -s "$PIDFILE" ] || return 1
    _p=$(cat "$PIDFILE" 2>/dev/null)
    case "$_p" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$_p" 2>/dev/null || return 1
    _args=$("$BUSYBOX" tr '\000' ' ' < "/proc/$_p/cmdline" 2>/dev/null) || return 1
    case "$_args" in
        *httpd*" -p $PORT "*) ;;
        *) return 1 ;;
    esac
    case "$_args" in
        *"-h $WWW"*) ;;
        *) return 1 ;;
    esac
    PID=$_p
    return 0
}

# 端口是否已在监听（/proc/net/tcp，状态 0A = LISTEN；端口是十六进制）
port_listening() {
    _hex=$(printf '%04X' "$PORT")
    "$BUSYBOX" awk -v p=":$_hex" '$2 ~ (p "$") && $4 == "0A" { f = 1 } END { exit !f }' \
        /proc/net/tcp 2>/dev/null
}

# 按 cmdline 找本端口的 httpd。旧版本是 daemonize 启动的，pidfile 里记的是
# 立刻退出的父进程 —— 升级后第一次 start 会遇到「端口被占但 alive 为假」。
# 这里把真正在跑的那个认出来，先收编再启动。
find_httpd_pids() {
    for d in /proc/[0-9]*; do
        _p=${d#/proc/}
        [ "$_p" = "$$" ] && continue
        _a=$("$BUSYBOX" tr '\000' ' ' < "$d/cmdline" 2>/dev/null) || continue
        case "$_a" in
            *httpd*" -p $PORT "*) ;;
            *) continue ;;
        esac
        case "$_a" in
            *"-h $WWW"*) echo "$_p" ;;
        esac
    done
}

# 等一组 pid 退出（最多 2s），然后 SIGKILL 剩余
stop_pids() {
    [ -n "${1:-}" ] || return 0
    _list=$1
    for _p in $_list; do kill "$_p" 2>/dev/null; done
    _i=0
    while [ "$_i" -lt 20 ]; do
        _left=
        for _p in $_list; do kill -0 "$_p" 2>/dev/null && _left=1; done
        [ -z "$_left" ] && break
        _i=$((_i + 1))
        "$BUSYBOX" usleep 100000 2>/dev/null || sleep 1
    done
    for _p in $_list; do kill -9 "$_p" 2>/dev/null; done
}

case "$1" in
stop)
    control_lock || { echo "ERR another capdtm-httpd operation is running" >&2; exit 1; }
    if alive; then
        _found=$PID
    else
        # pidfile 无效（旧版本 daemonize 记的是已退出的父进程）或已被删：
        # 以 cmdline 为准把真正在跑的那个认出来。
        _found=$(find_httpd_pids)
    fi
    if [ -n "$_found" ]; then
        stop_pids "$_found"
        echo "stopped (pid $_found)"
    else
        echo "not running"
    fi
    "$BUSYBOX" rm -f "$PIDFILE" 2>/dev/null
    ;;
start|status|"")
    control_lock || { echo "ERR another capdtm-httpd operation is running" >&2; exit 1; }
    if alive; then
        echo "already running (pid $PID, port $PORT)"
        exit 0
    fi
    if [ "${1:-}" = status ]; then
        echo "not running"
        exit 1
    fi
    [ -d "$WWW" ] || { echo "ERR $WWW is missing" >&2; exit 1; }
    [ -x "$BUSYBOX" ] || { echo "ERR $BUSYBOX is missing" >&2; exit 1; }
    # 旧版本 daemonize 留下的 httpd：pidfile 无效但端口被占。
    # 按 cmdline 收编再启动，否则升级后第一次 start 必然失败。
    if port_listening; then
        _stale=$(find_httpd_pids)
        if [ -n "$_stale" ]; then
            echo "adopting stale httpd (pid $_stale)" >&2
            stop_pids "$_stale"
        fi
    fi
    if port_listening; then
        echo "ERR port $PORT is already in use by a process that is not our httpd" >&2
        exit 1
    fi
    # -f：前台运行，$! 才是服务进程。
    # 9>&-：别让常驻的 httpd 继承控制锁的 fd，否则锁永不释放。
    "$BUSYBOX" httpd -f -p "$PORT" -h "$WWW" >/dev/null 2>&1 9>&- &
    PID=$!
    printf '%s\n' "$PID" > "$PIDFILE"
    _i=0
    while [ "$_i" -lt 20 ]; do
        if ! kill -0 "$PID" 2>/dev/null; then
            "$BUSYBOX" rm -f "$PIDFILE"
            echo "ERR httpd exited during startup (port $PORT in use?)" >&2
            exit 1
        fi
        if port_listening; then
            echo "started (pid $PID, port $PORT)"
            exit 0
        fi
        _i=$((_i + 1))
        "$BUSYBOX" usleep 100000 2>/dev/null || sleep 1
    done
    echo "ERR httpd did not listen on $PORT within 2s" >&2
    # 启动没成的话不能把它留在后台：会变成一个不监听、也认不出来的孤儿
    kill "$PID" 2>/dev/null
    "$BUSYBOX" rm -f "$PIDFILE"
    exit 1
    ;;
*)
    echo "usage: capdtm-httpd.sh start|stop|status" >&2
    exit 2
    ;;
esac

exit 0
