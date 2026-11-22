#!/bin/sh
# Build nxthumb.arm — the JPEG preview slicer used by the web gallery.
#
# No libc: the program talks to the kernel directly. The camera ships glibc 2.13
# and links against a 3.5 kernel, so a statically linked modern glibc (2.39 on
# this host) would emit futex/statx calls the device does not have.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
NX_CC=${NX_CC:-arm-linux-gnueabi-gcc}
OUT=${OUT:-$HERE/build}

mkdir -p "$OUT"
"$NX_CC" -Os -march=armv5te -mfloat-abi=soft -Wall -Wextra -Werror \
    -fno-stack-protector -fno-pic -no-pie -fno-builtin \
    -D_FILE_OFFSET_BITS=32 \
    -nostdlib -nostartfiles -Wl,-e,_start -Wl,--build-id=none \
    -o "$OUT/nxthumb.arm" "$HERE/nxthumb.c" "$HERE/crt0_arm.S" -lgcc

size=$(wc -c < "$OUT/nxthumb.arm")
echo "built $OUT/nxthumb.arm ($size bytes)"
mkdir -p "$ROOT/scripts/nx-rc/thumb"
cp "$OUT/nxthumb.arm" "$ROOT/scripts/nx-rc/thumb/nxthumb.arm"
chmod 755 "$ROOT/scripts/nx-rc/thumb/nxthumb.arm"
sha256sum "$OUT/nxthumb.arm"
