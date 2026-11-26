#!/bin/sh
# Install only the pinned Log117 closure. Does not activate gamma or rebuild fast start.
set -eu
SRC=${GAMMA_SRC:-/mnt/mmc/metol-extensions/log117}
DST=${GAMMA_DST:-/opt/usr/nx-ks/gamma}
VERSION=${GAMMA_VERSION_FILE:-/etc/version.info}
RUN=${GAMMA_RUN:-/tmp/nx-lut-lite}
PIN=a30ad550a68653d475aee8201e20dfbd16fd91802f2f5a41ea2e49c60fdcbf6f
fail() { printf 'RESULT=%s\n' "$1"; exit 1; }
hash() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"
    else /opt/usr/nx-ks/busybox sha256sum "$1"; fi | cut -d' ' -f1
}
verify() {
    [ -d "$1" ] && [ ! -L "$1" ] && [ -f "$1/payload.sha256" ] && [ ! -L "$1/payload.sha256" ] || return 1
    [ "$(hash "$1/payload.sha256")" = "$PIN" ] || return 1
    while read -r expected name; do
        [ -f "$1/$name" ] && [ ! -L "$1/$name" ] || return 1
        # All names come from the pinned manifest; reject linked parents too.
        part=$name
        while [ "$part" != "${part%/*}" ]; do
            part=${part%/*}; [ ! -L "$1/$part" ] || return 1
        done
        [ "$(hash "$1/$name")" = "$expected" ] || return 1
    done < "$1/payload.sha256"
}
mode=${1:-install}
case "$mode" in check|install) ;; *) fail USAGE_INSTALL_OR_CHECK;; esac
if [ "$mode" = check ]; then
    verify "$DST" || fail VERIFY_FAILED
    printf 'RESULT=PAYLOAD_VERIFIED\nACTIVATED=false\n'; exit 0
fi
grep -q '^NX500$' "$VERSION" && grep -q '^1.12$' "$VERSION" || fail UNSUPPORTED_FIRMWARE
verify "$SRC" || fail SOURCE_VERIFY_FAILED
[ ! -e "$RUN/clean-log.policy" ] && [ ! -e "$RUN/session/ready" ] && [ ! -e "$RUN/clean-log-command.lock" ] || fail ACTIVE_GAMMA_SESSION
if verify "$DST"; then
    printf 'RESULT=ALREADY_INSTALLED\nACTIVATED=false\n'; exit 0
fi
parent=${DST%/*}
[ -d "$parent" ] && [ ! -L "$parent" ] && [ ! -L "$DST" ] || fail TARGET_UNAVAILABLE
lock=$parent/.gamma-install.lock
mkdir "$lock" 2>/dev/null || fail INSTALL_BUSY
stage=$parent/.gamma-log117-new-$$
backup=$parent/gamma-before-log117-$(date +%Y%m%d%H%M%S)-$$
trap 'rmdir "$lock" 2>/dev/null || :' 0
mkdir "$stage" || fail STAGING_FAILED
while read -r expected name; do
    mkdir -p "$stage/$(dirname "$name")" || fail MKDIR_FAILED
    cp "$SRC/$name" "$stage/$name" || fail COPY_FAILED
    case "$name" in *.sh|bin/*|compat/*) chmod 755 "$stage/$name";; *) chmod 644 "$stage/$name";; esac
done < "$SRC/payload.sha256"
cp "$SRC/payload.sha256" "$stage/payload.sha256"
verify "$stage" || fail STAGED_VERIFY_FAILED
# Preserve runtime-owned state when replacing an installed RC5 tree.
if [ -e "$DST/state" ]; then
    [ -d "$DST/state" ] && [ ! -L "$DST/state" ] || fail STATE_CONFLICT
    cp -a "$DST/state" "$stage/state" || fail STATE_COPY_FAILED
fi
if [ -e "$DST" ]; then mv "$DST" "$backup" || fail BACKUP_RENAME_FAILED; fi
if ! mv "$stage" "$DST"; then
    [ ! -e "$backup" ] || mv "$backup" "$DST"
    fail PUBLISH_FAILED
fi
sync
printf 'RESULT=INSTALLED\nPROFILE=log117\nACTIVATED=false\nBACKUP=%s\n' "$backup"
