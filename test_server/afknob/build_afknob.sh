#!/bin/sh
# @gate script=build_afknob.sh opens=none one_shot=1 gate=none pconly=1
# AF 跟踪灵敏度三参数工具（只读 status 可单独上机；set 需相机在线）
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NX_CC=${NX_CC:-arm-linux-gnueabi-gcc}
mkdir -p "$HERE/build"
"$NX_CC" -O0 -Wall -Wextra -Werror -nostdlib -static -fno-builtin \
    -fno-stack-protector -fno-pic -no-pie -fomit-frame-pointer \
    -marm -mfloat-abi=soft -Wl,-e,_start \
    -o "$HERE/build/nx_afknob.arm" "$HERE/nx_afknob.c"
md5sum "$HERE/build/nx_afknob.arm"
