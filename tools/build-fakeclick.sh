#!/bin/bash
# 交叉编译 fakeclick（相机 ABI：armv5t softfp + glibc 2.13）
set -euo pipefail
RF=${NXUI_ROOTFS:-/opt/nxks2/rootfs-112}
cd "$(dirname "$0")"
INC=../test_server/nxui/out/deps/usr/include
CRT=../test_server/filmlab/src/crt0_arm.S
arm-linux-gnueabi-gcc -std=c99 -O0 -Wall -I"$INC" \
    -U_TIME_BITS -D_TIME_BITS=32 -U_FILE_OFFSET_BITS -D_FILE_OFFSET_BITS=32 \
    -mfloat-abi=soft -fno-pic -no-pie \
    -nostdlib -nostartfiles -Wl,-e,_start -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -Wl,--allow-shlib-undefined -Wl,-rpath-link,"$RF/usr/lib" -Wl,-rpath-link,"$RF/lib" \
    fakeclick.c "$CRT" "$RF/usr/lib/libX11.so.6" "$RF/usr/lib/libXtst.so.6" \
    "$RF/lib/libc-2.13.so" -lgcc -o fakeclick.arm
arm-linux-gnueabi-readelf -d fakeclick.arm | grep NEEDED
ls -lh fakeclick.arm
