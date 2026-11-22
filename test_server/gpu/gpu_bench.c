/* gpu_bench.c — NX500 Mali-400 MP 小尺寸图像处理基准（v3）
 *
 * v1 的五个错误（2026-11-21 用户指出，全部已修）：
 *   1. shader/program/texture 在 eglMakeCurrent 之前创建 —— 无 current context，对象非法
 *      → 现在：先建 context + surface 并 makeCurrent，之后才创建所有 GL 对象
 *   2. 每个尺寸重建 context 且未共享，却复用 GL 对象
 *      → 现在：context 只建一次；每尺寸只换 pbuffer surface，context 不变
 *   3. 未检查编译/链接/GL 错误 —— 失败调用会被测成"很快"
 *      → 现在：编译/链接、GL错误、逐像素CPU参考比较通过才输出
 *   4. now_ns() 用 ARM 32 位 long 存绝对纳秒（2.1 s 溢出）
 *      → 现在：64 位无符号，只取差值
 *   5. LUT 绿通道 (i*3)>>1 溢出回绕
 *      → 现在：显式 clamp 到 255
 *
 * 运行前置：EGL 走 X11，DISPLAY 必须设置（SSH 会话里默认为空，这是 0x3008 的成因）
 *   DISPLAY=:0 LD_PRELOAD=/usr/lib/libnative-buffer.so ./gpu_bench.arm 5
 *
 * 构建：
 *   arm-linux-gnueabi-gcc -O2 -nostdlib -fno-builtin -Wl,-e,_start \
 *       -Wl,--allow-shlib-undefined -o gpu_bench.arm gpu_bench.c syslib/libdl-2.13.so -lgcc
 *
 * BENCH keeps the three stage minima; total_* are complete-iteration timings.
 * CPU is an integer reference, not JPEG decoding or an optimized scaler.
 */
typedef unsigned int U;
typedef void *P;
typedef int I;
typedef unsigned long long U64;

#include "bench_reference.h"

extern P dlopen(const char *, I);
extern P dlsym(P, const char *);
extern char *dlerror(void);

#define MAXS 256   /* 相机常驻内存仅 ~6MB，BSS 压到 512KB */

static unsigned char src[MAXS * MAXS * 4];
static unsigned char dst[MAXS * MAXS * 4];
static unsigned char lut[256 * 4];
static U64 total_samples[BENCH_MAX_REPS], cpu_samples[BENCH_MAX_REPS];
static U64 gpu_cpu_samples[BENCH_MAX_REPS];
static volatile U cpu_sink;
static I resident_mode;

/* ---------------- 输出 / syscall ---------------- */
static void out(const char *s)
{
    U n = 0;
    while (s[n]) n++;
    register I r0 __asm__("r0") = 1;
    register const char *r1 __asm__("r1") = s;
    register U r2 __asm__("r2") = n;
    register I r7 __asm__("r7") = 4;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}

static void out(const char *s);

/* 复用 out()：临时补 NUL 后转调。emit 自己的 svc 序列在本工具链下会卡住 */
static void emit(char *b, char *p)
{
    char saved = *p;
    *p = 0;
    out(b);
    *p = saved;
}

static void out_u(U v);

static char *putu(char *p, U v)
{
    char t[12];
    I k = 0;
    if (!v) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k) *p++ = t[--k];
    return p;
}

static char *puthex(char *p, U v)
{
    const char *h = "0123456789abcdef";
    I i;
    *p++ = '0'; *p++ = 'x';
    for (i = 28; i >= 0; i -= 4) *p++ = h[(v >> i) & 15];
    return p;
}

static char *puts_(char *p, const char *s) { while (*s) *p++ = *s; return p; }

static void out_u(U v)
{
    char t[16];
    char *p = putu(t, v);
    *p = 0;
    out(t);
}

/* libgcc 的除法辅助只在除零时调 raise；-nostdlib 需自备 */
int raise(int sig) { (void)sig; return 0; }

struct ts { I sec; I nsec; };

static U64 clock_ns(I clock_id)
{
    struct ts t = {0, 0};
    register long r0 __asm__("r0") = clock_id;
    register long r1 __asm__("r1") = (long)&t;
    register long r7 __asm__("r7") = 263;    /* clock_gettime */
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    if (r0 != 0) return 0;
    return (U64)(U)t.sec * 1000000000ULL + (U64)(U)t.nsec;
}

static U64 now_ns(void) { return clock_ns(1); }

static I atoi_simple(const char *s)
{
    I v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > BENCH_MAX_REPS) return 0;
        s++;
    }
    return *s ? 0 : v;
}

static void report(U size, const char *op, U64 up, U64 dr, U64 rd, U nz,
                   U samples, U error)
{
    /* 输出一律走 out()：栈缓冲 + emit 的写法在本工具链上会卡死 */
    out("BENCH,");
    out_u(size);
    out(",");
    out(op);
    out(",");
    out_u((U)(up / 1000ULL));
    out(",");
    out_u((U)(dr / 1000ULL));
    out(",");
    out_u((U)(rd / 1000ULL));
    out(",nz=");
    out_u(nz);
    out(",samples="); out_u(samples);
    out(",max_error="); out_u(error);
    out(",total_min_us="); out_u((U)(total_samples[0] / 1000ULL));
    out(",total_p50_us="); out_u((U)(bench_percentile(total_samples, samples, 50) / 1000ULL));
    out(",total_p95_us="); out_u((U)(bench_percentile(total_samples, samples, 95) / 1000ULL));
    out(",cpu_p50_us="); out_u((U)(bench_percentile(cpu_samples, samples, 50) / 1000ULL));
    out(",cpu_p95_us="); out_u((U)(bench_percentile(cpu_samples, samples, 95) / 1000ULL));
    out(",gpu_cpu_p50_us="); out_u((U)(bench_percentile(gpu_cpu_samples, samples, 50) / 1000ULL));
    out(resident_mode ? ",mode=resident\n" : ",mode=roundtrip\n");
}

/* ---------------- shaders ---------------- */
static const char *VS =
    "attribute vec2 pos;varying vec2 uv;"
    "void main(){uv=pos*0.5+0.5;gl_Position=vec4(pos,0.0,1.0);}";

static const char *FS_COPY =
    "precision mediump float;varying vec2 uv;uniform sampler2D tex;"
    "void main(){gl_FragColor=texture2D(tex,uv);}";

static const char *FS_MAT =
    "precision mediump float;varying vec2 uv;uniform sampler2D tex;uniform mat3 m;"
    "void main(){vec4 c=texture2D(tex,uv);"
    "gl_FragColor=vec4(clamp(m*vec3(c.rgb),0.0,1.0),c.a);}";

static const char *FS_LUT =
    "precision mediump float;varying vec2 uv;uniform sampler2D tex;uniform sampler2D l;"
    "void main(){vec4 c=texture2D(tex,uv);"
    "vec3 t=(c.rgb*255.0+0.5)/256.0;"
    "float r=texture2D(l,vec2(t.r,0.5)).r;"
    "float g=texture2D(l,vec2(t.g,0.5)).g;"
    "float b=texture2D(l,vec2(t.b,0.5)).b;"
    "gl_FragColor=vec4(r,g,b,c.a);}";

#define FN(ret, name, args) ret(*name) args = dlsym(egl, #name); if (!name) { out("EGL sym missing: " #name "\n"); return 2; }
#define GFN(ret, name, args) ret(*name) args = dlsym(gl, #name); if (!name) { out("GL sym missing: " #name "\n"); return 7; }

static int run(int reps)
{
    const char *dbg;

    out("START\n");
    if (!now_ns() || !clock_ns(2)) { out("FAIL benchmark clocks unavailable\n"); return 15; }
    /* 只靠 LD_PRELOAD 提供 libnative-buffer；在 -nostdlib 下主动 dlopen 它会卡死 */
    P egl = dlopen("libEGL.so.1", 2);
    if (!egl) { dbg = dlerror(); if (dbg) out(dbg); out("\n"); return 1; }
    out("EGL_LOADED\n");

    FN(P, eglGetDisplay, (P));
    FN(U, eglInitialize, (P, I *, I *));
    FN(U, eglChooseConfig, (P, const I *, P *, I, I *));
    FN(U, eglBindAPI, (U));
    FN(P, eglCreatePbufferSurface, (P, P, const I *));
    FN(P, eglCreateContext, (P, P, P, const I *));
    FN(U, eglMakeCurrent, (P, P, P, P));
    FN(U, eglDestroyContext, (P, P));
    FN(U, eglDestroySurface, (P, P));
    FN(U, eglTerminate, (P));
    FN(I, eglGetError, (void));
    FN(const char *, eglQueryString, (P, I));

    out("BEFORE_INIT|");
    P d = eglGetDisplay(0);
    I ma = 0, mi = 0;
    if (!d || !eglInitialize(d, &ma, &mi)) {
        char b[48], *p = b;
        p = puts_(p, "eglInitialize fail ");
        p = puthex(p, (U)eglGetError()); *p++ = '\n';
        emit(b, p);
        return 3;
    }
    out("INIT_OK|");
    {
        const char *vs = eglQueryString(d, 0x3054);
        out("Q_OK|");
        out("EGL_VERSION ");
        out(vs ? vs : "?");
        out("\n");
    }
    out("CFG_ENTER|");

    I attrs[] = { 0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038 };
    P cfg = 0;
    I num = 0;
    if (!eglChooseConfig(d, attrs, &cfg, 1, &num) || !num) { out("no pbuffer config\n"); return 4; }
    out("CFG_OK|");
    /* 与探针一致：chooseConfig 之后再 bindAPI（Mali r3p2 有顺序依赖） */
    if (!eglBindAPI(0x30a0)) { out("eglBindAPI fail\n"); return 3; }
    out("API_OK|");



    /* ---- ① context 只建一次，先 makeCurrent 到 16×16 临时 surface ---- */
    I ca[] = { 0x3098, 2, 0x3038 };
    I pa0[] = { 0x3057, 16, 0x3056, 16, 0x3038 };
    out("SYMS_OK|CTX_TRY|");
    P s0 = eglCreatePbufferSurface(d, cfg, pa0);
    out("SURF_OK|");
    P ctx = eglCreateContext(d, cfg, 0, ca);
    out("CTX_OK|");
    if (!ctx || !s0 || !eglMakeCurrent(d, s0, s0, ctx)) {
        char b[40], *p = b;
        p = puts_(p, "makeCurrent fail ");
        p = puthex(p, (U)eglGetError()); *p++ = '\n';
        emit(b, p);
        return 5;
    }
    out("CURRENT_OK|");
    out("GLLOAD|");
    P gl = dlopen("libGLESv2.so.2", 2);
    if (!gl) { dbg = dlerror(); if (dbg) out(dbg); out("\n"); return 6; }
    out("GLLOADED|");

    GFN(const unsigned char *, glGetString, (U));
    GFN(void, glGetIntegerv, (U, I *));
    GFN(U, glCreateShader, (U));
    GFN(void, glShaderSource, (U, I, const char **, const I *));
    GFN(void, glCompileShader, (U));
    GFN(void, glGetShaderiv, (U, U, I *));
    GFN(U, glCreateProgram, (void));
    GFN(void, glAttachShader, (U, U));
    GFN(void, glBindAttribLocation, (U, U, const char *));
    GFN(void, glLinkProgram, (U));
    GFN(void, glGetProgramiv, (U, U, I *));
    GFN(void, glUseProgram, (U));
    GFN(void, glVertexAttribPointer, (U, I, U, U, I, const void *));
    GFN(void, glEnableVertexAttribArray, (U));
    GFN(void, glDrawArrays, (U, I, I));
    GFN(void, glViewport, (I, I, I, I));
    GFN(void, glClearColor, (float, float, float, float));
    GFN(void, glClear, (U));
    GFN(void, glGenTextures, (I, U *));
    GFN(void, glBindTexture, (U, U));
    GFN(void, glTexImage2D, (U, I, I, I, I, I, U, U, const void *));
    GFN(void, glTexParameteri, (U, U, I));
    GFN(void, glActiveTexture, (U));
    GFN(I, glGetUniformLocation, (U, const char *));
    GFN(void, glUniform1i, (I, I));
    GFN(void, glUniformMatrix3fv, (I, I, U, const float *));
    GFN(void, glReadPixels, (I, I, I, I, U, U, P));
    GFN(U, glGetError, (void));
    GFN(void, glFinish, (void));
    GFN(void, glDisable, (U));
    glDisable(0x0bd0); /* GL_DITHER: compare fixed RGBA8 reference values. */
    /* ---- ② 现在有 current context，可以安全创建对象 ---- */
    {
        I mt = 0;
        glGetIntegerv(0xd33, &mt);
        out("GL_RENDERER ");
        out((const char *)glGetString(0x1f01));
        out("\nMAX_TEX ");
        out_u((U)mt);
        out("\n");
    }

    {
        I y, x, i;
        for (y = 0; y < MAXS; y++) {
            for (x = 0; x < MAXS; x++) {
                unsigned char *q = src + ((U)y * MAXS + x) * 4;
                q[0] = (unsigned char)(x & 0xff);
                q[1] = (unsigned char)(y & 0xff);
                q[2] = (unsigned char)(((x ^ y) >> 2) & 0xff);
                q[3] = 255;
            }
        }
        for (i = 0; i < 256; i++) {
            I g = (i * 3) / 2;
            if (g > 255) g = 255;
            lut[i * 4 + 0] = (unsigned char)(255 - i);
            lut[i * 4 + 1] = (unsigned char)g;
            lut[i * 4 + 2] = (unsigned char)i;
            lut[i * 4 + 3] = 255;
        }
    }

    /* program 构建（带状态检查） */
    U progs[3];
    const char *fss[3];
    const char *names[3];
    fss[0] = FS_COPY; names[0] = "copy";
    fss[1] = FS_MAT;  names[1] = "mat";
    fss[2] = FS_LUT;  names[2] = "lut";

    {
        I k;
        for (k = 0; k < 3; k++) {
            U v = glCreateShader(0x8b31), f = glCreateShader(0x8b30), prog;
            I ok = 0;
            glShaderSource(v, 1, &VS, 0);
            glCompileShader(v);
            glShaderSource(f, 1, &fss[k], 0);
            glCompileShader(f);
            glGetShaderiv(v, 0x8b81, &ok);
            if (!ok) { out("VS compile fail\n"); return 8; }
            glGetShaderiv(f, 0x8b81, &ok);
            if (!ok) { out("FS compile fail\n"); return 8; }
            prog = glCreateProgram();
            glAttachShader(prog, v);
            glAttachShader(prog, f);
            glBindAttribLocation(prog, 0, "pos");
            glLinkProgram(prog);
            glGetProgramiv(prog, 0x8b82, &ok);
            if (!ok) { out("link fail\n"); return 8; }
            progs[k] = prog;
        }
    }

    out("SHADERS_OK|");
    U tex_src = 0, tex_lut = 0;
    glActiveTexture(0x84c0);
    glGenTextures(1, &tex_src);
    glBindTexture(0x0de1, tex_src);
    glTexParameteri(0x0de1, 0x2801, 0x2601);
    glTexParameteri(0x0de1, 0x2800, 0x2601);
    glTexParameteri(0x0de1, 0x2802, 0x812f);
    glTexParameteri(0x0de1, 0x2803, 0x812f);

    glActiveTexture(0x84c1);
    glGenTextures(1, &tex_lut);
    glBindTexture(0x0de1, tex_lut);
    glTexParameteri(0x0de1, 0x2801, 0x2601);
    glTexParameteri(0x0de1, 0x2800, 0x2601);
    glTexParameteri(0x0de1, 0x2802, 0x812f);
    glTexParameteri(0x0de1, 0x2803, 0x812f);
    glTexImage2D(0x0de1, 0, 0x1908, 256, 1, 0, 0x1908, 0x1401, lut);
    glActiveTexture(0x84c0);

    if (glGetError() != 0) { out("setup GL error\n"); return 9; }

    static const float verts[6] = { -1, -1, 3, -1, -1, 3 };
    static const float mat3x3[9] = { 0.9f, 0.05f, 0.05f,
                                     0.05f, 0.9f, 0.05f,
                                     0.05f, 0.05f, 0.9f };

    I sizes[2] = { 128, 256 };
    I si, oi, it;

    for (si = 0; si < 2; si++) {
        U S = (U)sizes[si];
        I pa[] = { 0x3057, (I)S, 0x3056, (I)S, 0x3038 };
        P surf = eglCreatePbufferSurface(d, cfg, pa);
        if (!surf || !eglMakeCurrent(d, surf, surf, ctx)) {
            out("surface/current fail\n");
            if (surf) eglDestroySurface(d, surf);
            return 10;
        }
        out("VP_OK|");
        glViewport(0, 0, (I)S, (I)S);

        for (oi = 0; oi < 3; oi++) {
            U64 b_up = 0, b_dr = 0, b_rd = 0;
            U nz = 0, max_error = 0;
            I u_tex, u_mat, u_lut;

            {
                out("OP ");
                out_u(S);
                out(" ");
                out(names[oi]);
                out("\n");
            }
            glUseProgram(progs[oi]);
            u_tex = glGetUniformLocation(progs[oi], "tex");
            if (u_tex >= 0) glUniform1i(u_tex, 0);
            u_mat = glGetUniformLocation(progs[oi], "m");
            if (u_mat >= 0) glUniformMatrix3fv(u_mat, 1, 0, mat3x3);
            u_lut = glGetUniformLocation(progs[oi], "l");
            if (u_lut >= 0) glUniform1i(u_lut, 1);

            for (it = 0; it < reps + 2; it++) {
                U i, sum = 0;
                U64 c0 = now_ns(), c1;
                bench_cpu(src, dst, MAXS, S, oi);
                c1 = now_ns();
                for (i = 0; i < S * S * 4u; i++) sum += dst[i];
                cpu_sink = sum;
                if (it >= 2) cpu_samples[it - 2] = c1 - c0;
            }

            if (resident_mode) {
                glActiveTexture(0x84c0);
                glBindTexture(0x0de1, tex_src);
                glTexImage2D(0x0de1, 0, 0x1908, MAXS, MAXS, 0, 0x1908, 0x1401, src);
                glFinish();
                if (glGetError() != 0) { out("FAIL resident upload\n"); return 9; }
            }

            for (it = 0; it < reps + 2; it++) {
                U64 t0, t1, t2, t3, c0;
                U err, i;

                glActiveTexture(0x84c0);
                glBindTexture(0x0de1, tex_src);
                for (i = 0; i < S * S * 4u; i++) dst[i] = 0;
                c0 = clock_ns(2);
                t0 = now_ns();
                if (!resident_mode) {
                    glTexImage2D(0x0de1, 0, 0x1908, MAXS, MAXS, 0, 0x1908, 0x1401, src);
                    glFinish();
                }
                t1 = now_ns();

                glClearColor(0, 0, 0, 1);
                glClear(0x4000);
                glVertexAttribPointer(0, 2, 0x1406, 0, 0, verts);
                glEnableVertexAttribArray(0);
                if (oi == 2) { glActiveTexture(0x84c1); glBindTexture(0x0de1, tex_lut); }
                t2 = now_ns();
                glDrawArrays(0x0004, 0, 3);
                glFinish();
                t3 = now_ns();

                err = glGetError();
                if (err != 0) {
                    char b[48], *p = b;
                    p = puts_(p, "GL error in op ");
                    p = puts_(p, names[oi]);
                    *p++ = ' '; p = puthex(p, err); *p++ = '\n';
                    emit(b, p);
                    return 9;
                }

                if (it >= 2) {
                    U64 du = t1 - t0, dd = t3 - t2, dr;
                    U64 r0, r1;
                    U error;
                    r0 = now_ns();
                    if (!resident_mode) glReadPixels(0, 0, (I)S, (I)S, 0x1908, 0x1401, dst);
                    r1 = resident_mode ? t3 : now_ns();
                    gpu_cpu_samples[it - 2] = clock_ns(2) - c0;
                    /* Resident mode measures synchronized draw only; the validation
                     * read is deliberately outside both wall and process CPU time. */
                    if (resident_mode) glReadPixels(0, 0, (I)S, (I)S, 0x1908, 0x1401, dst);
                    if (glGetError() != 0) { out("FAIL readback GL error\n"); return 11; }
                    error = bench_error(src, dst, MAXS, S, oi);
                    if (error > max_error) max_error = error;
                    if (error > BENCH_TOLERANCE) {
                        out("FAIL pixel mismatch max_error="); out_u(error); out("\n");
                        return 12;
                    }
                    dr = resident_mode ? 0 : r1 - r0;
                    total_samples[it - 2] = r1 - t0;
                    if (!resident_mode && (!b_up || du < b_up)) b_up = du;
                    if (!b_dr || dd < b_dr) b_dr = dd;
                    if (!b_rd || dr < b_rd) b_rd = dr;
                }
            }

            /* Legacy output diagnostic; pixel correctness was checked above. */
            {
                U i;
                U total = S * S * 4u;
                if (total > sizeof(dst)) total = sizeof(dst);
                for (i = 0; i < total; i++) if (dst[i]) nz++;
            }
            if (!b_dr || (!resident_mode && (!b_up || !b_rd))) { out("FAIL incomplete samples\n"); return 13; }
            bench_sort(total_samples, (U)reps);
            bench_sort(cpu_samples, (U)reps);
            bench_sort(gpu_cpu_samples, (U)reps);
            report(S, names[oi], b_up, b_dr, b_rd, nz, (U)reps, max_error);
        }

        eglMakeCurrent(d, 0, 0, 0);
        eglDestroySurface(d, surf);
    }

    eglDestroyContext(d, ctx);
    eglTerminate(d);
    return 0;
}

/* 入口：sp 必须在任何 prologue 之前取，否则读到的是被改过的栈指针 */
static int nx_main(unsigned *sp) __attribute__((used));

static int nx_main(unsigned *sp)
{
    I argc = (I)sp[0];
    char **argv = (char **)(sp + 1);
    I reps = (argc > 1) ? atoi_simple(argv[1]) : 20;
    if (argc == 3) {
        const char *p = argv[2], *q = "resident";
        while (*p && *p == *q) { p++; q++; }
        resident_mode = !*p && !*q;
    }
    if (reps <= 0 || reps > BENCH_MAX_REPS || argc > 3 || (argc == 3 && !resident_mode)) {
        out("usage: gpu_bench.arm [reps:1..100] [resident]\n");
        return 14;
    }
    return run(reps);
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #1\n"
        "svc 0\n");
