#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bench_reference.h"

int main(void)
{
    unsigned char src[4 * 4 * 4], dst[sizeof src], p[4];
    unsigned long long values[] = {100, 1, 5, 3, 2};
    unsigned x, y;
    int op;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            unsigned char *q = src + (y * 4 + x) * 4;
            q[0] = (unsigned char)(x * 60);
            q[1] = (unsigned char)(y * 60);
            q[2] = 120;
            q[3] = 255;
        }
    }
    bench_pixel(src, 4, 4, 3, 2, 0, p);
    assert(p[0] == 180 && p[1] == 120 && p[2] == 120 && p[3] == 255);
    bench_pixel(src, 4, 2, 0, 0, 0, p);
    assert(p[0] == 30 && p[1] == 30 && p[2] == 120 && p[3] == 255);
    bench_pixel(src, 4, 4, 3, 2, 1, p);
    assert(p[0] == 174 && p[1] == 123 && p[2] == 123);
    bench_pixel(src, 4, 4, 3, 3, 2, p);
    assert(p[0] == 75 && p[1] == 255 && p[2] == 120);
    for (op = 0; op < 3; op++) {
        bench_cpu(src, dst, 4, 4, op);
        assert(bench_error(src, dst, 4, 4, op) == 0);
        bench_cpu(src, dst, 4, 2, op);
        assert(bench_error(src, dst, 4, 2, op) == 0);
        memset(dst, 0, sizeof dst);
        for (x = 3; x < sizeof dst; x += 4) dst[x] = 255;
        assert(bench_error(src, dst, 4, 2, op) > BENCH_TOLERANCE);
    }
    bench_cpu(src, dst, 4, 4, 0);
    assert(bench_error(src, dst, 4, 4, 2) > BENCH_TOLERANCE);
    dst[0] = 4;
    assert(bench_error(src, dst, 4, 4, 0) == 4);
    bench_sort(values, 5);
    assert(values[0] == 1 && values[4] == 100);
    assert(bench_percentile(values, 5, 50) == 3);
    assert(bench_percentile(values, 5, 95) == 100);
    assert(bench_percentile(values, 1, 95) == 1);
    puts("PASS: copy/matrix/LUT, 1x/2x scaling, opaque-clear/stale/corrupt rejection, min/p50/p95");
    return 0;
}
