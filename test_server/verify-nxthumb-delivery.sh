#!/bin/bash
# Verifies nxthumb.arm actually travels with the release.
#
# Background: the binary is a cross-compiled artifact, and .gitignore excluded
# *.arm wholesale. A clean clone therefore packaged without it, and the gallery
# silently fell back to ImageMagick -- 8-20 s per frame instead of ~150 ms.
# These assertions pin the delivery chain: git tracked, packed into both zips,
# copied by the incremental updater, and executable after an upgrade.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=scripts/nx-rc/thumb/nxthumb.arm
FAIL=0

say() { printf '%s\n' "$*"; }
fail() { say "FAIL: $*"; FAIL=$((FAIL + 1)); }
pass() { say "PASS: $*"; }

[ -f "$BIN" ] || { echo "FAIL: $BIN missing on disk"; exit 1; }

# 1) tracked by git, so a clean clone has it
if [ -n "$(git ls-files -- "$BIN")" ]; then
    pass "git tracks $BIN"
else
    fail "$BIN is not tracked -- a clean clone would ship without it"
fi

if git check-ignore -q --no-index -- "$BIN"; then
    fail "$BIN still matches an ignore rule"
else
    pass "$BIN is exempt from the *.arm ignore rule"
fi

# 2) both language packages include it (--list prints "<size>  <path>")
for lang in zh en; do
    if python3 tools/pack_release.py --lang "$lang" --list 2>/dev/null |
        awk -v want="$BIN" '$NF == want { found = 1 } END { exit !found }'; then
        pass "pack_release --lang $lang includes $BIN"
    else
        fail "pack_release --lang $lang omits $BIN"
    fi
done

# 3) the incremental updater copies it (it only globbed *.sh before)
if grep -q 'nxthumb\.arm' scripts/update_nxrc.sh; then
    pass "update_nxrc.sh copies nxthumb.arm"
else
    fail "update_nxrc.sh never copies nxthumb.arm"
fi

# 4) simulate upgrading an old machine that lacks the binary
TMP=$(mktemp -d)
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$TMP/sd/nx-rc/thumb" "$TMP/cam/nx-rc/thumb" "$TMP/cam/nx-rc/capdtm/www/cgi-bin"
cp -f scripts/nx-rc/thumb/*.sh scripts/nx-rc/thumb/*-cgi "$TMP/sd/nx-rc/thumb/" 2>/dev/null
cp -f "$BIN" "$TMP/sd/nx-rc/thumb/"
chmod 755 "$TMP/sd/nx-rc/thumb/"* 2>/dev/null
# old machine: same scripts, no binary
cp -f "$TMP/sd/nx-rc/thumb/thumb-cgi" "$TMP/cam/nx-rc/thumb/thumb-cgi"
echo 'old' > "$TMP/cam/nx-rc/thumb/thumb-cgi"

NX_UPDATE_SRC="$TMP/sd/nx-rc" NX_UPDATE_DST="$TMP/cam/nx-rc" \
    NX_UPDATE_ROOT="$TMP/cam" NX_INSTALL_ROOT="$TMP/cam" \
    bash scripts/update_nxrc.sh >"$TMP/update.log" 2>&1
rc=$?

if [ "$rc" -ne 0 ]; then
    fail "update_nxrc.sh exited $rc on a normal upgrade; log follows"
    sed 's/^/    /' "$TMP/update.log" | tail -12
elif [ ! -f "$TMP/cam/nx-rc/thumb/nxthumb.arm" ]; then
    fail "upgrade left no nxthumb.arm on the machine"
elif [ ! -x "$TMP/cam/nx-rc/thumb/nxthumb.arm" ]; then
    fail "nxthumb.arm landed without the execute bit -- the CGI would skip it"
elif ! cmp -s "$BIN" "$TMP/cam/nx-rc/thumb/nxthumb.arm"; then
    fail "upgraded nxthumb.arm differs from the packaged one"
elif ! cmp -s scripts/nx-rc/thumb/thumb-cgi "$TMP/cam/nx-rc/thumb/thumb-cgi"; then
    fail "thumb-cgi was not refreshed during the upgrade"
elif [ ! -f "$TMP/cam/nx-rc/capdtm/www/cgi-bin/thumb" ]; then
    fail "thumb-install.sh did not publish the CGI after the upgrade"
else
    pass "upgrading an old machine installs an executable nxthumb.arm and republishes the CGI"
fi

# 5) an incomplete SD master must be refused, not silently degraded
rm -f "$TMP/sd/nx-rc/thumb/nxthumb.arm"
if NX_UPDATE_SRC="$TMP/sd/nx-rc" NX_UPDATE_DST="$TMP/cam/nx-rc" \
    NX_UPDATE_ROOT="$TMP/cam" NX_INSTALL_ROOT="$TMP/cam" \
    bash scripts/update_nxrc.sh >"$TMP/refuse.log" 2>&1; then
    fail "update_nxrc.sh accepted an SD master without nxthumb.arm"
elif grep -q 'nxthumb.arm' "$TMP/refuse.log"; then
    pass "an SD master missing nxthumb.arm is rejected with a clear message"
else
    fail "rejected the incomplete master but never named the missing file"
fi

say ''
if [ "$FAIL" -eq 0 ]; then
    say "RESULT: PASS  delivery chain intact"
    exit 0
fi
say "RESULT: FAIL  $FAIL problem(s)"
exit 1
