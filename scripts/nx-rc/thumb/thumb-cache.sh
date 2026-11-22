#!/bin/sh
# Shared by the CGI and prewarm worker. The lock lives off the SD card.
THUMB_BB=/opt/usr/nx-ks/busybox
THUMB_TOOLS=/opt/usr/nx-ks/nx-rc/tools
THUMB_STATE=/tmp/nx/thumb
THUMB_NXTHUMB=/opt/usr/nx-ks/nx-rc/thumb/nxthumb.arm

thumb_find_sd() {
    for SD in /mnt/mmc /tmp/mmc /mnt/sd /tmp/sd; do
        [ -d "$SD/DCIM" ] && return 0
    done
    SD=
    return 1
}

thumb_dimensions() {
    W=$1 Q=$2
    case "$W" in ''|*[!0-9]*) W=400 ;; esac
    case "$Q" in ''|*[!0-9]*) Q=82 ;; esac
    # Keep numeric tests in range even if a request supplies a very long number.
    while [ "${W#0}" != "$W" ]; do W=${W#0}; done
    while [ "${Q#0}" != "$Q" ]; do Q=${Q#0}; done
    W=${W:-0} Q=${Q:-0}
    if [ "${#W}" -gt 4 ] || [ "$W" -gt 1024 ]; then W=1024; fi
    if [ "${#Q}" -gt 2 ] || [ "$Q" -gt 95 ]; then Q=95; fi
    [ "$W" -ge 64 ] || W=64
    [ "$Q" -ge 50 ] || Q=50
}

thumb_paths() {
    SRC="$SD/DCIM/$1"
    CACHE="$SD/.thumbcache/${2}x${3}/$1.jpg"
}

thumb_ready() {
    [ -s "$CACHE" ] && [ ! "$SRC" -nt "$CACHE" ]
}

# True when the original already carries a preview wide enough for $1, so the
# slicer can answer without ImageMagick. Prewarm uses this to skip whole swathes
# of the card: caching a derived image the camera wrote itself is wasted work.
thumb_sliceable() {
    ts_want=$1
    [ -x "$THUMB_NXTHUMB" ] || return 1
    case "$SRC" in *.JPG|*.jpg|*.JPEG|*.jpeg) ;; *) return 1 ;; esac
    ts_meta=$("$THUMB_NXTHUMB" info "$SRC" 2>/dev/null) || return 1
    [ -n "$ts_meta" ] || return 1
    ts_len=${ts_meta%% *}
    ts_rest=${ts_meta#* }
    ts_rest=${ts_rest#* }
    ts_big=${ts_rest%% *}
    case "$ts_len" in ''|*[!0-9]*) return 1 ;; esac
    if [ "$ts_want" -le 340 ]; then
        [ "$ts_len" -gt 0 ]
        return
    fi
    case "$ts_big" in ''|*[!0-9]*) return 1 ;; esac
    [ "$ts_big" -gt 0 ]
}

# The MPF frame is the better lightbox source by a wide margin. Measured over
# Wi-Fi on the device with a 13.9 MB original:
#   convert, cache cold   7452 ms (20030 ms from a clean page cache)
#   convert, cache warm    399 ms
#   MPF slice              697 ms
# Caching only wins for a frame that was prewarmed, and prewarming a lightbox
# frame costs those same 7.5-20 s each. Default on; touch $THUMB_STATE/mpf-off
# to fall back to the converter.
thumb_mpf_enabled() {
    [ -f "$THUMB_STATE/mpf-off" ] && return 1
    return 0
}

# Run at most one cold generation across all clients and the prewarm worker.
# A kernel lock is released when the last holder exits, including after a kill.
thumb_generate() {
    thumb_ready && return 0
    mkdir -p "$THUMB_STATE" || return 1
    (
        attempts=0
        until "$THUMB_BB" flock -n 9; do
            thumb_ready && exit 0
            # 32 x 0.25s = 8s. The old 31s ceiling stacked on top of the convert
            # timeout, so one stuck request held a CGI child for nearly a minute;
            # 8s + 20/30s keeps the worst case around 40s.
            [ "$attempts" -lt 32 ] || exit 1
            attempts=$((attempts + 1))
            "$THUMB_BB" sleep 0.25
        done
        # BusyBox timeout's watchdog inherits fd 9. Unlock explicitly after
        # conversion and publishing, even while the watchdog is exiting.
        tmp=
        trap '[ -z "$tmp" ] || rm -f "$tmp"; "$THUMB_BB" flock -u 9' EXIT
        trap 'exit 1' HUP INT TERM
        thumb_ready && exit 0
        [ -f "$SRC" ] && [ -x "$THUMB_TOOLS/usr/bin/convert" ] || exit 1
        # A 1024px conversion peaks at ~31 MiB RSS (measured). On this 3.5 kernel
        # there is no MemAvailable, and MemFree alone misleads -- the converter
        # drives it to ~8 MB while the kernel reclaims page cache. What matters
        # is MemFree + Cached. Refuse rather than gamble: an OOM here takes down
        # the camera daemons and needs a battery pull to recover.
        avail=$(awk '/^MemFree:/{f=$2} /^Cached:/{c=$2} END{printf "%d", f+c}' /proc/meminfo)
        case "$avail" in ''|*[!0-9]*) avail=0 ;; esac
        [ "$avail" -ge 40960 ] || exit 1
        mkdir -p "${CACHE%/*}" || exit 1
        tmp="$CACHE.tmp.$$"
        memory=24MiB
        budget=20
        # Measured on the device, 13.9 MB original: 320px needs ~8s and 31 MB of
        # peak RSS is only enough for the 512-and-below path; 1024px needs ~20s
        # and ~31 MB of pixel cache, so it gets a wider budget. Dropping the
        # jpeg:size hint makes 1024px fail outright at 32MiB.
        if [ "$1" -gt 512 ]; then
            memory=32MiB
            budget=30
        fi
        export LD_LIBRARY_PATH="$THUMB_TOOLS/usr/lib"
        "$THUMB_BB" timeout -t "$budget" -s KILL "$THUMB_BB" nice -n 19 \
            "$THUMB_TOOLS/usr/bin/convert" -limit thread 1 -limit memory "$memory" \
            -limit map 0 -limit disk 0 -define "jpeg:size=${1}x${1}" \
            "${SRC}[0]" -auto-orient -thumbnail "${1}x${1}" -quality "$2" "jpeg:$tmp" 8>&- \
            || exit 1
        [ -s "$tmp" ] || exit 1
        # This exFAT implementation leaves duplicate entries on rename-overwrite.
        rm -f "$CACHE" && mv "$tmp" "$CACHE"
    ) 9>"$THUMB_STATE/convert.lock" 2>/dev/null
}
