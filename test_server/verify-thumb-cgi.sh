#!/bin/sh
# Run with sh from a Linux environment (or WSL).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
FIXTURE=$(mktemp -d)
trap 'rm -rf -- "$FIXTURE"' EXIT
mkdir -p "$FIXTURE/sd/DCIM/100PHOTO" "$FIXTURE/tools/usr/bin"
touch "$FIXTURE/sd/DCIM/100PHOTO/A.JPG" "$FIXTURE/sd/DCIM/100PHOTO/B.JPG"

cat > "$FIXTURE/tools/usr/bin/convert" <<'EOF'
#!/bin/sh
for arg do output=$arg; done
test "${output#jpeg:}" != "$output" || exit 1
mkdir "$CONVERT_RUNNING" || { echo overlap >> "$CONVERT_LOG"; exit 1; }
trap 'rmdir "$CONVERT_RUNNING"' EXIT
echo convert >> "$CONVERT_LOG"
if [ "${CONVERT_FAIL:-0}" = 1 ]; then
    echo partial > "${output#jpeg:}"
    exit 1
fi
sleep 0.3
echo 'fixture preview' > "${output#jpeg:}"
EOF
chmod +x "$FIXTURE/tools/usr/bin/convert"
cat > "$FIXTURE/busybox" <<'EOF'
#!/bin/sh
if [ "$1" = timeout ]; then
    seconds=$3 signal=$5
    shift 5
    exec timeout -s "$signal" "$seconds" "$@"
fi
exec "$@"
EOF
chmod +x "$FIXTURE/busybox"
THUMB=$ROOT/scripts/nx-rc/thumb
sed "s|^THUMB_DIR=.*|THUMB_DIR=$FIXTURE|" "$THUMB/thumb-cgi" > "$FIXTURE/thumb-cgi"
sed -e "s|/mnt/mmc|$FIXTURE/sd|" -e "s|^THUMB_TOOLS=.*|THUMB_TOOLS=$FIXTURE/tools|" \
    -e "s|^THUMB_BB=.*|THUMB_BB=$FIXTURE/busybox|" -e "s|^THUMB_STATE=.*|THUMB_STATE=$FIXTURE/state|" \
    "$THUMB/thumb-cache.sh" > "$FIXTURE/thumb-cache.sh"
cp "$THUMB/thumb-prewarm.sh" "$FIXTURE/"
export QUERY_STRING='f=100PHOTO/A.JPG&w=1024&q=85'
export CONVERT_LOG="$FIXTURE/convert.log"
export CONVERT_RUNNING="$FIXTURE/convert.running"

timeout 3 sh "$FIXTURE/thumb-cgi" > "$FIXTURE/first" &
first=$!
timeout 3 sh "$FIXTURE/thumb-cgi" > "$FIXTURE/second" &
second=$!
wait "$first"
wait "$second"
timeout 3 sh "$FIXTURE/thumb-cgi" > "$FIXTURE/cached"

test "$(wc -l < "$CONVERT_LOG")" -eq 1
cmp "$FIXTURE/first" "$FIXTURE/second"
cmp "$FIXTURE/first" "$FIXTURE/cached"
grep -qx 'Content-Type: image/jpeg' "$FIXTURE/first"
grep -qx 'fixture preview' "$FIXTURE/first"
test ! -e "$FIXTURE/sd/.thumbcache/1024x85/100PHOTO/A.JPG.jpg.lock"
echo 'PASS: cold nested directory, concurrent requests, cache hit and lock cleanup'

sh "$FIXTURE/thumb-prewarm.sh" start 320 75 2 1024 85 2
sh "$FIXTURE/thumb-prewarm.sh" start 320 75 2 1024 85 2
QUERY_STRING='f=100PHOTO/B.JPG&w=1024&q=85' timeout 6 sh "$FIXTURE/thumb-cgi" > "$FIXTURE/b" &
foreground=$!
wait "$foreground"
i=0
until sh "$FIXTURE/thumb-prewarm.sh" json | grep -q '"running":0.*"total":2'; do
    i=$((i + 1))
    test "$i" -lt 60
    sleep 0.1
done
for name in A B; do
    for size in '320x75' '1024x85'; do
        test -s "$FIXTURE/sd/.thumbcache/$size/100PHOTO/$name.JPG.jpg"
        QUERY_STRING="f=100PHOTO/$name.JPG&w=${size%x*}&q=${size#*x}" sh "$FIXTURE/thumb-cgi" |
            grep -qx 'fixture preview'
    done
done
test "$(wc -l < "$CONVERT_LOG")" -eq 4
! grep -q overlap "$CONVERT_LOG"
echo 'PASS: prewarm shares both cache keys and one converter with online requests'

cache="$FIXTURE/sd/.thumbcache/1024x85/100PHOTO/A.JPG.jpg"
touch -t 200001010000 "$cache"
CONVERT_FAIL=1 sh "$FIXTURE/thumb-cgi" | grep -qx 'thumb error: thumbnail generation failed'
grep -qx 'fixture preview' "$cache"
test -z "$(find "$FIXTURE/sd/.thumbcache" -name '*.tmp.*')"
sh "$FIXTURE/thumb-cgi" | grep -qx 'fixture preview'
echo 'PASS: failed converter cannot publish a partial image and retry works'

sed "s|/mnt/mmc|$FIXTURE/sd|" "$THUMB/dirlist-cgi" > "$FIXTURE/dirlist-cgi"
mkdir "$FIXTURE/sd/DCIM/100PHOTO/child"
touch "$FIXTURE/sd/DCIM/100PHOTO/with space.JPG" "$FIXTURE/sd/DCIM/100PHOTO/with\"quote.JPG"
touch "$FIXTURE/sd/DCIM/100PHOTO/.hidden"
QUERY_STRING='p=100PHOTO' sh "$FIXTURE/dirlist-cgi" > "$FIXTURE/dirlist"
python3 - "$FIXTURE/dirlist" <<'PY'
import json
from pathlib import Path
import sys

listing = json.loads(Path(sys.argv[1]).read_text().split("\n\n", 1)[1])
assert listing == {
    "ok": True, "path": "100PHOTO", "dirs": ["child"],
    "files": ["A.JPG", "B.JPG", "with space.JPG", 'with"quote.JPG'],
}, listing
PY
echo 'PASS: directory JSON preserves filenames and separates directories'

# ---- 直切：真机原片里已存好的预览图，不该再起 convert ----
# Needs a real NX500 frame; skipped on machines without the sample corpus.
SAMPLE=${NXTHUMB_SAMPLE:-$ROOT/../_photos/SAM_0001.JPG}
PORTRAIT=${NXTHUMB_PORTRAIT:-$ROOT/../_photos/SAM_0063.JPG}
if [ -x "$THUMB/nxthumb.arm" ] && [ -f "$SAMPLE" ]; then
    mkdir -p "$FIXTURE/sd/DCIM/200PHOTO"
    cp "$THUMB/nxthumb.arm" "$FIXTURE/nxthumb.arm"
    ln -s "$SAMPLE" "$FIXTURE/sd/DCIM/200PHOTO/land.JPG"
    : > "$CONVERT_LOG"

    QUERY_STRING='f=200PHOTO/land.JPG&w=320&q=75' sh "$FIXTURE/thumb-cgi" > "$FIXTURE/slice"
    python3 - "$FIXTURE/slice" "$FIXTURE/nxthumb.arm" "$SAMPLE" <<'PY'
import subprocess
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_bytes()
head, body = raw.split(b"\r\n\r\n" if b"\r\n\r\n" in raw[:400] else b"\n\n", 1)
assert b"Content-Type: image/jpeg" in head, head[:120]
declared = int([l.split(b":")[1] for l in head.splitlines()
                if l.lower().startswith(b"content-length")][0])
assert declared == len(body), (declared, len(body))
assert body[:2] == b"\xff\xd8" and body[-2:] == b"\xff\xd9", "not a whole JPEG"
info = subprocess.run([sys.argv[2], "info", sys.argv[3]], capture_output=True).stdout.split()
assert int(info[0]) == len(body), (int(info[0]), len(body))
assert int(info[1]) == 1, "landscape sample should not need rotation"
PY
    test ! -s "$CONVERT_LOG"
    echo 'PASS: grid request slices the embedded preview without running convert'

    # The lightbox defaults to the MPF frame; the off switch restores convert.
    mkdir -p "$FIXTURE/state"
    : > "$FIXTURE/state/mpf-off"
    QUERY_STRING='f=200PHOTO/land.JPG&w=1024&q=85' sh "$FIXTURE/thumb-cgi" > /dev/null
    test -s "$CONVERT_LOG"
    : > "$CONVERT_LOG"
    rm -f "$FIXTURE/state/mpf-off"
    QUERY_STRING='f=200PHOTO/land.JPG&w=1024&q=85' sh "$FIXTURE/thumb-cgi" > "$FIXTURE/big"
    python3 - "$FIXTURE/big" "$FIXTURE/nxthumb.arm" "$SAMPLE" <<'PY'
import subprocess
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_bytes()
head, body = raw.split(b"\r\n\r\n" if b"\r\n\r\n" in raw[:400] else b"\n\n", 1)
info = subprocess.run([sys.argv[2], "info", sys.argv[3]], capture_output=True).stdout.split()
assert int(info[2]) == len(body), (int(info[2]), len(body))
assert body[:2] == b"\xff\xd8" and body[-2:] == b"\xff\xd9"
PY
    test ! -s "$CONVERT_LOG"
    echo 'PASS: MPF lightbox is the default and the off switch restores convert'

    # Portrait frames need the injected orientation tag to come out upright.
    if [ -f "$PORTRAIT" ]; then
        ln -s "$PORTRAIT" "$FIXTURE/sd/DCIM/200PHOTO/port.JPG"
        : > "$CONVERT_LOG"
        QUERY_STRING='f=200PHOTO/port.JPG&w=320&q=75' sh "$FIXTURE/thumb-cgi" > "$FIXTURE/pslice"
        python3 - "$FIXTURE/pslice" "$FIXTURE/nxthumb.arm" "$PORTRAIT" <<'PY'
import subprocess
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_bytes()
head, body = raw.split(b"\r\n\r\n" if b"\r\n\r\n" in raw[:400] else b"\n\n", 1)
info = subprocess.run([sys.argv[2], "info", sys.argv[3]], capture_output=True).stdout.split()
stored, orient = int(info[0]), int(info[1])
assert orient == 8, orient
assert len(body) == stored + 36, (len(body), stored)
# body = FFD8 + APP1, so the segment header starts at index 2:
#   [2:4] = FFE1 marker  [4:6] = 0x0022 length  [6:12] = "Exif\0\0"
# and the orientation value sits at 2 + 29.
assert body[2:4] == b"\xff\xe1", body[2:6].hex()
assert body[4:6] == b"\x00\x22", "APP1 length must cover exactly one entry"
assert body[6:12] == b"Exif\x00\x00", body[6:12]
assert body[31] == 8, body[31]
PY
        test ! -s "$CONVERT_LOG"
        echo 'PASS: portrait frame gains a 36-byte orientation tag and skips convert'
    fi
fi

