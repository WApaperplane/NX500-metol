#ifndef NX_GPU_BENCH_REFERENCE_H
#define NX_GPU_BENCH_REFERENCE_H

#define BENCH_MAX_REPS 100
#define BENCH_TOLERANCE 3

/* Integer reference for the benchmark's 1x/2x bilinear reduction and shaders.
 * Intermediate rounding differs from mediump GLSL; compare within tolerance. */
static void bench_pixel(const unsigned char *src, unsigned source_size,
                        unsigned size, unsigned x, unsigned y, int op,
                        unsigned char pixel[4])
{
    unsigned step = source_size / size, dx, dy, c, v[4] = {0, 0, 0, 0};
    unsigned count = step * step;
    for (dy = 0; dy < step; dy++)
        for (dx = 0; dx < step; dx++)
            for (c = 0; c < 4; c++)
                v[c] += src[((y * step + dy) * source_size + x * step + dx) * 4 + c];
    for (c = 0; c < 4; c++) v[c] = (v[c] + count / 2) / count;
    if (op == 1) {
        for (c = 0; c < 3; c++)
            pixel[c] = (unsigned char)((85 * v[c] + 5 * (v[0] + v[1] + v[2]) + 50) / 100);
    } else if (op == 2) {
        pixel[0] = (unsigned char)(255 - v[0]);
        pixel[1] = (unsigned char)(v[1] * 3 / 2 > 255 ? 255 : v[1] * 3 / 2);
        pixel[2] = (unsigned char)v[2];
    } else {
        for (c = 0; c < 3; c++) pixel[c] = (unsigned char)v[c];
    }
    pixel[3] = (unsigned char)v[3];
}

static void bench_cpu(const unsigned char *src, unsigned char *dst,
                      unsigned source_size, unsigned size, int op)
{
    unsigned x, y;
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
            bench_pixel(src, source_size, size, x, y, op, dst + (y * size + x) * 4);
}

/* Return the maximum channel error; alpha participates, so an opaque clear
 * or a stale frame cannot pass just because some output bytes are nonzero. */
static unsigned bench_error(const unsigned char *src, const unsigned char *dst,
                            unsigned source_size, unsigned size, int op)
{
    unsigned x, y, c, max_error = 0;
    unsigned char pixel[4];
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            bench_pixel(src, source_size, size, x, y, op, pixel);
            for (c = 0; c < 4; c++) {
                int d = (int)dst[(y * size + x) * 4 + c] - (int)pixel[c];
                unsigned e = (unsigned)(d < 0 ? -d : d);
                if (e > max_error) max_error = e;
            }
        }
    }
    return max_error;
}

static void bench_sort(unsigned long long *values, unsigned count)
{
    unsigned i, j;
    for (i = 1; i < count; i++) {
        unsigned long long v = values[i];
        for (j = i; j > 0 && values[j - 1] > v; j--) values[j] = values[j - 1];
        values[j] = v;
    }
}

/* Nearest rank, 1 <= count <= BENCH_MAX_REPS; sort before calling. */
static unsigned long long bench_percentile(const unsigned long long *values,
                                           unsigned count, unsigned percentile)
{
    return values[(count * percentile + 99) / 100 - 1];
}

#endif
