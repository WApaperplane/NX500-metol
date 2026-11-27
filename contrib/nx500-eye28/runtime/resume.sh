#!/bin/sh
helper="$(dirname "$0")/../out/eye28-verify.arm"
if [ -x "$helper" ]; then
  result=$("$helper" resume "$@" 2>/dev/null && printf '.') && case "$result" in 'NATIVE
.'|'ENABLE
.') printf '%s\n' "${result%??}"; exit 0;; esac
fi
echo NATIVE
exit 0
