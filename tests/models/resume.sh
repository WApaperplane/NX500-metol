#!/bin/sh
# Offline selection only; never launches a process.
set -u
f=${1-}
[ -d "$f" ] || { echo NATIVE; exit 0; }
[ -s "$f/launch_status" ] && [ "$(cat "$f/launch_status")" = PASS ] || { echo NATIVE; exit 0; }
[ -s "$f/already_started" ] && [ "$(cat "$f/already_started")" = NO ] || { echo NATIVE; exit 0; }
result=$(sh "$(dirname "$0")/selector.sh" "$f") || { echo NATIVE; exit 0; }
[ "$result" = CANDIDATE ] && echo ENABLE || echo NATIVE
