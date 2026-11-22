#!/bin/bash
# 构建 pwfast.arm / pwprobe.arm（NX500，armv7l，相机 glibc 2.15 前的版本）
#
# 为什么 -nostdlib：相机是 2015 年的 Tizen，Ubuntu 24.04 的 arm-linux-gnueabi-gcc
# 链的是 glibc 2.39，直接链出来的二进制在相机上跑不起来。这里只借 libdl.so.2
# （test_server/gpu/syslib/ 里有 2.13 版副本），其余（write/nanosleep/
# clock_gettime/exit_group/数字格式化）全走原生 syscall 与自写实现。
#
# ★ _start 必须用 exit_group(248)。用 syscall 1(exit) 只退主线程，
#   dlopen 进来的库若起了线程，进程会变僵尸（2026-11-22 实测残留 6 个）。
#
# 交叉编译器不在 PATH 时用 ARM_CC 指定，例如：
#   ARM_CC=/usr/bin/arm-linux-gnueabi-gcc ./build_fast.sh
set -euo pipefail
cd "$(dirname "$0")"

CC="${ARM_CC:-arm-linux-gnueabi-gcc}"
DL=../gpu/syslib/libdl-2.13.so
FLAGS=(-O2 -nostdlib -fno-builtin -fno-stack-protector
       -Wall -Wextra -Werror -Wl,-e,_start -Wl,--allow-shlib-undefined)

[ -f "$DL" ] || { echo "缺少 $DL（libdl 2.13 副本）" >&2; exit 1; }

for t in pwfast pwprobe; do
    "$CC" "${FLAGS[@]}" -o "$t.arm" "$t.c" "$DL" -lgcc
    printf '%-12s ' "$t.arm"
    "${CC%gcc}size" "$t.arm" | sed -n '2p'
done

echo "--- 版本需求（应只有 GLIBC_2.4 一级）---"
"${CC%gcc}readelf" -d pwfast.arm | sed -n '/NEEDED/p'
"${CC%gcc}readelf" -V pwfast.arm | sed -n '/Name: GLIBC/p' | sort -u
