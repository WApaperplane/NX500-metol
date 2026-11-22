#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
"${HOST_CC:-cc}" -O2 -std=c99 -Wall -Wextra -Werror \
    test_bench_reference.c -o out/test_bench_reference
out/test_bench_reference
"${ARM_CC:-arm-linux-gnueabi-gcc}" -O2 -nostdlib -fno-builtin \
    -fno-stack-protector -Wall -Wextra -Werror -Wl,-e,_start \
    -Wl,--allow-shlib-undefined -o out/gpu_bench.arm \
    gpu_bench.c syslib/libdl-2.13.so -lgcc
arm-linux-gnueabi-size out/gpu_bench.arm
arm-linux-gnueabi-readelf -d out/gpu_bench.arm | sed -n '/NEEDED/p'
