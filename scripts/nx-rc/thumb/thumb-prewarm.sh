#!/bin/sh
# Prewarm the newest grid entries and a smaller set of lightbox previews.
SELF=${0%/*}
[ "$SELF" != "$0" ] || SELF=.
SELF=$(cd "$SELF" && pwd) || exit 1
[ -r "$SELF/thumb-cache.sh" ] || exit 1
. "$SELF/thumb-cache.sh"

PIDFILE=$THUMB_STATE/prewarm.pid
PROGRESS=$THUMB_STATE/prewarm.json
LOGFILE=$THUMB_STATE/prewarm.log

worker_pid() {
    [ -r "$PIDFILE" ] && read -r worker < "$PIDFILE" || return 1
    case "$worker" in ''|*[!0-9]*|0|1) return 1 ;; esac
    kill -0 "$worker" 2>/dev/null || return 1
    # The process can exit between the liveness test and this read. Redirect
    # both the command and the fd-open so a lost pidfile race stays silent.
    cmd=$( { tr '\000' ' ' < "/proc/$worker/cmdline"; } 2>/dev/null ) || return 1
    case "$cmd" in
        *"$SELF/thumb-prewarm.sh worker "*) return 0 ;;
    esac
    return 1
}

write_progress() {
    printf '{"running":%s,"w":%s,"q":%s,"fw":%s,"fq":%s,"done":%s,"skipped":%s,"failed":%s,"total":%s,"ts":%s}\n' \
        "$1" "$W" "$Q" "$FW" "$FQ" "$done_count" "$skipped" "$failed" "$total" "$(date +%s)" \
        > "$PROGRESS.tmp" && mv -f "$PROGRESS.tmp" "$PROGRESS"
}

case "${1:-}" in
    stop)
        worker_pid && kill "$worker" 2>/dev/null
        echo "thumb-prewarm stopped"
        exit 0
        ;;
    status)
        if worker_pid; then echo "running (pid $worker)"; else echo "not running"; fi
        echo "--- progress ---"
        cat "$PROGRESS" 2>/dev/null
        echo "--- last log ---"
        tail -5 "$LOGFILE" 2>/dev/null
        exit 0
        ;;
    json)
        if [ ! -s "$PROGRESS" ]; then
            echo '{"running":0,"done":0,"skipped":0,"total":0}'
        elif worker_pid; then
            cat "$PROGRESS"
        else
            sed 's/"running":1/"running":0/' "$PROGRESS"
        fi
        exit 0
        ;;
    start)
        mkdir -p "$THUMB_STATE" || exit 1
        shift
        sh "$SELF/thumb-prewarm.sh" worker "$@" </dev/null >/dev/null 2>&1 &
        echo "thumb-prewarm requested"
        exit 0
        ;;
    worker) ;;
    *) echo "usage: thumb-prewarm.sh start [width quality count full_width full_quality full_count]|stop|status|json"; exit 1 ;;
esac

# The worker owns its own PID and kernel lock, so concurrent starts and stale
# pidfiles do not spawn another scan.
exec 8>"$THUMB_STATE/prewarm.lock" || exit 1
"$THUMB_BB" flock -n 8 || exit 0
printf '%s\n' "$$" > "$PIDFILE" || exit 1
list="$THUMB_STATE/files.$$"
trap 'rm -f "$PIDFILE" "$list" "$PROGRESS.tmp"' EXIT
trap 'exit 1' HUP INT TERM

thumb_dimensions "${5:-1024}" "${6:-85}"
FW=$W FQ=$Q
thumb_dimensions "${2:-320}" "${3:-75}"
limit=${4:-200}
full_limit=${7:-40}
case "$limit" in *[!0-9]*) limit=200 ;; esac
case "$full_limit" in *[!0-9]*) full_limit=40 ;; esac

done_count=0 skipped=0 failed=0 total=0 sliced=0
write_progress 1
waited=0
until thumb_find_sd; do
    [ "$waited" -lt "${SD_WAIT:-30}" ] || { write_progress 0; exit 1; }
    waited=$((waited + 1))
    sleep 1
done

# Bound the work to the newest requested entries, even when most are cached.
find "$SD/DCIM" -mindepth 2 -maxdepth 2 -type f \
    \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' \) 2>/dev/null |
    LC_ALL=C sort -r | head -n "$limit" > "$list"
total=$(wc -l < "$list")
write_progress 1

seen=0
while IFS= read -r file; do
    relative=${file#"$SD/DCIM/"}
    seen=$((seen + 1))
    thumb_paths "$relative" "$W" "$Q"
    if thumb_sliceable "$W"; then
        # The camera already stored a 320px preview, so the CGI answers from the
        # original file and there is nothing to cache. Counting these apart from
        # "skipped" keeps the log honest about what actually ran.
        sliced=$((sliced + 1))
    elif thumb_ready; then
        skipped=$((skipped + 1))
    elif thumb_generate "$W" "$Q"; then
        done_count=$((done_count + 1))
    else
        failed=$((failed + 1))
    fi

    if [ "$seen" -le "$full_limit" ]; then
        thumb_paths "$relative" "$FW" "$FQ"
        if thumb_mpf_enabled && thumb_sliceable "$FW"; then
            : # lightbox is served from the original, nothing to cache
        else
            thumb_generate "$FW" "$FQ" || :
        fi
    fi
    [ $((seen % 5)) -ne 0 ] || write_progress 1
done < "$list"

write_progress 0
printf 'generated=%s sliced=%s skipped=%s failed=%s total=%s\n' \
    "$done_count" "$sliced" "$skipped" "$failed" "$total" > "$LOGFILE"
