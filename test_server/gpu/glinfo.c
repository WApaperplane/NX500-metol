/* glinfo.c — NX500 Mali-400 MP 的 GL 能力清单（只读）
 * 构建：arm-linux-gnueabi-gcc -O2 -nostdlib -fno-builtin -fno-stack-protector \
 *         -Wl,-e,_start -Wl,--allow-shlib-undefined -o glinfo.arm glinfo.c \
 *         syslib/libdl-2.13.so -lgcc
 * 运行：DISPLAY=:0 LD_PRELOAD=/usr/lib/libnative-buffer.so ./glinfo.arm [esver]
 */
typedef unsigned int U;
typedef int I;
typedef void *P;

extern P dlopen(const char *, I);
extern P dlsym(const char *, const char *);

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

static char buf[1024];

static void emit(char *p)
{
    *p = 0;
    out(buf);
}

static char *putu(char *p, U v)
{
    char t[12];
    I k = 0;
    if (!v) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k) *p++ = t[--k];
    return p;
}

static char *puts_(char *p, const char *s) { while (*s) *p++ = *s; return p; }

static void kv(const char *k, I v)
{
    /* 不用共享 buf：-nostdlib + -O2 下多次写同一全局数组容易出问题，
     * 每个函数自带局部缓冲最稳（实测 ks() 用全局 buf 会 segfault）。 */
    char t[16];
    char *p = putu(t, (U)v);
    *p = 0;
    out(k);
    out(t);
    out("\n");
}

static void ks(const char *k, const char *v)
{
    out(k);
    out(v ? v : "(null)");
    out("\n");
}

static I has_ext(const char *list, const char *name)
{
    U n = 0, i;
    while (name[n]) n++;
    if (!list) return 0;
    for (i = 0; list[i]; ) {
        U j = 0, k = 0;
        while (list[i + j] && list[i + j] != ' ') j++;
        if (j == n) {
            while (k < n && list[i + k] == name[k]) k++;
            if (k == n) return 1;
        }
        i += j;
        while (list[i] == ' ') i++;
    }
    return 0;
}

/* 带上限的字符串输出：GL 返回的指针若不可靠，裸 out() 会跑飞（实测 hang） */
static void outs(const char *tag, const char *s)
{
    I k;
    out(tag);
    if (!s) { out("(null)"); out("\n"); return; }
    for (k = 0; k < 200 && s[k]; k++) {
        char c[2];
        c[0] = s[k];
        c[1] = 0;
        out(c);
    }
    out("\n");
}

int nx_main(U *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    int esver = 2;

    P egl, gl, d, cfg, s, ctx;
    const char *egs;
    I ma = 0, mi = 0, num = 0, v = 0;
    I attrs[13];
    I ca[3];
    I pa[5];
    const char *ext;

    /* EGL 函数指针 */
    P (*p_eglGetDisplay)(P);
    U (*p_eglInitialize)(P, I *, I *);
    U (*p_eglChooseConfig)(P, const I *, P *, I, I *);
    U (*p_eglBindAPI)(U);
    P (*p_eglCreatePbufferSurface)(P, P, const I *);
    P (*p_eglCreateContext)(P, P, P, const I *);
    U (*p_eglMakeCurrent)(P, P, P, P);
    const char *(*p_eglQueryString)(P, I);

    const unsigned char *(*p_glGetString)(U);
    void (*p_glGetIntegerv)(U, I *);
    void (*p_glGetShaderPrecisionFormat)(U, U, I *, I *);

    if (argc > 1 && argv[1][0] == '1') esver = 1;

    out("S1 start\n");

    egl = dlopen("libEGL.so.1", 2);
    if (!egl) { out("dlopen libEGL failed\n"); return 1; }
    out("S2 dlopen ok\n");

    p_eglGetDisplay = dlsym(egl, "eglGetDisplay");
    p_eglInitialize = dlsym(egl, "eglInitialize");
    p_eglChooseConfig = dlsym(egl, "eglChooseConfig");
    p_eglBindAPI = dlsym(egl, "eglBindAPI");
    p_eglCreatePbufferSurface = dlsym(egl, "eglCreatePbufferSurface");
    p_eglCreateContext = dlsym(egl, "eglCreateContext");
    p_eglMakeCurrent = dlsym(egl, "eglMakeCurrent");
    p_eglQueryString = dlsym(egl, "eglQueryString");
    if (!p_eglGetDisplay || !p_eglInitialize || !p_eglChooseConfig) { out("EGL syms missing\n"); return 2; }
    out("S3 syms ok\n");
    if (!p_eglQueryString) out("WARN eglQueryString null\n");

    d = p_eglGetDisplay(0);
    if (!d) { out("eglGetDisplay null\n"); return 3; }
    if (!p_eglInitialize(d, &ma, &mi)) { out("eglInitialize failed\n"); return 3; }
    out("S4 init ok\n");

    {
        const char *vs;
        out("S4a before qs\n");
        vs = p_eglQueryString(d, 0x3054);
        out("S4b got vs\n");
        out("EGL_VERSION   ");
        out(vs ? vs : "(null)");
        out("\n");
        vs = p_eglQueryString(d, 0x3053);
        out("S4c got vendor\n");
        out("EGL_VENDOR    ");
        out(vs ? vs : "(null)");
        out("\n");
    }
    egs = p_eglQueryString(d, 0x3055);
    out("EGL_EXTENSIONS ");
    out(egs ? egs : "(null)");
    out("\n\n");
    out("S5 qs done\n");

    attrs[0] = 0x3033; attrs[1] = 1;
    attrs[2] = 0x3040; attrs[3] = 4;
    attrs[4] = 0x3024; attrs[5] = 8;
    attrs[6] = 0x3023; attrs[7] = 8;
    attrs[8] = 0x3022; attrs[9] = 8;
    attrs[10] = 0x3038;
    attrs[11] = 0; attrs[12] = 0;
    cfg = 0;
    if (!p_eglChooseConfig(d, attrs, &cfg, 1, &num) || !num) { out("no pbuffer config\n"); return 4; }
    if (!p_eglBindAPI(0x30a0)) { out("eglBindAPI failed\n"); return 3; }
    out("S6 cfg ok\n");

    ca[0] = 0x3098; ca[1] = esver; ca[2] = 0x3038;
    pa[0] = 0x3057; pa[1] = 16;
    pa[2] = 0x3056; pa[3] = 16;
    pa[4] = 0x3038;
    s = p_eglCreatePbufferSurface(d, cfg, pa);
    ctx = p_eglCreateContext(d, cfg, 0, ca);
    if (!ctx) { out("eglCreateContext failed\n"); return 5; }
    if (!s) { out("eglCreatePbufferSurface failed\n"); return 5; }
    if (!p_eglMakeCurrent(d, s, s, ctx)) { out("eglMakeCurrent failed\n"); return 5; }
    out("S7 current ok\n");

    gl = dlopen("libGLESv2.so.2", 2);
    if (!gl) { out("dlopen libGLESv2 failed\n"); return 6; }
    p_glGetString = dlsym(gl, "glGetString");
    p_glGetIntegerv = dlsym(gl, "glGetIntegerv");
    p_glGetShaderPrecisionFormat = dlsym(gl, "glGetShaderPrecisionFormat");
    if (!p_glGetString || !p_glGetIntegerv) { out("GL syms missing\n"); return 7; }
    out("S8 gl syms ok\n");

    /* Mali 的 libGLESv2 需要一次真实 GL 调用把分发表建起来，
     * gpu_bench 的顺序就是先 glGetIntegerv 再 glGetString。 */
    p_glGetIntegerv(0x0d33, &v);
    /* 紧跟一个 glGetString，与 gpu_bench 的调用顺序完全一致 */
    outs("GL_RENDERER   ", (const char *)p_glGetString(0x1f01));
    out("S9 maxtex=");
    {
        char t[16];
        char *q = putu(t, (U)v);
        *q = 0;
        out(t);
        out("\n");
    }
    v = 0;

    {
        char hdr[32];
        char *p = hdr;
        p = puts_(p, "=== GLES ");
        p = putu(p, (U)esver);
        p = puts_(p, " context ===");
        *p = 0;
        out(hdr);
        out("\n");
    }

    /* ★ GL_VENDOR(0x1f00) 在这台 Mali-400 上会让 glGetString 挂死（实测 12s 无返回），
     *   而 GL_RENDERER(0x1f01) 正常 —— 所以下面不查 VENDOR。 */
    outs("GL_VERSION    ", (const char *)p_glGetString(0x1f02));
    outs("GLSL_VERSION  ", (const char *)p_glGetString(0x8b8c));

    ext = (const char *)p_glGetString(0x1f03);
    out("GL_EXTENSIONS ");
    out(ext ? ext : "(null)");
    out("\n\n");

    out("--- 关键能力 ---\n");
    kv("OES_texture_3D               ", has_ext(ext, "GL_OES_texture_3D"));
    kv("OES_texture_float            ", has_ext(ext, "GL_OES_texture_float"));
    kv("OES_texture_half_float       ", has_ext(ext, "GL_OES_texture_half_float"));
    kv("EXT_shader_texture_lod       ", has_ext(ext, "GL_EXT_shader_texture_lod"));
    kv("OES_standard_derivatives     ", has_ext(ext, "GL_OES_standard_derivatives"));
    kv("OES_EGL_image                ", has_ext(ext, "GL_OES_EGL_image"));
    kv("OES_EGL_image_external       ", has_ext(ext, "GL_OES_EGL_image_external"));
    kv("ARM_shader_framebuffer_fetch ", has_ext(ext, "GL_ARM_shader_framebuffer_fetch"));
    kv("EXT_color_buffer_half_float  ", has_ext(ext, "GL_EXT_color_buffer_half_float"));
    kv("OES_fbo_render_mipmap        ", has_ext(ext, "GL_OES_fbo_render_mipmap"));
    kv("OES_vertex_half_float        ", has_ext(ext, "GL_OES_vertex_half_float"));
    kv("EXT_sRGB                     ", has_ext(ext, "GL_EXT_sRGB"));
    kv("OES_depth_texture            ", has_ext(ext, "GL_OES_depth_texture"));
    kv("EXT_texture_filter_anisotropic", has_ext(ext, "GL_EXT_texture_filter_anisotropic"));
    out("\n");

    out("--- 上限 ---\n");
    p_glGetIntegerv(0x0d33, &v); kv("MAX_TEXTURE_SIZE             ", v);
    p_glGetIntegerv(0x851c, &v); kv("MAX_CUBE_MAP_TEXTURE_SIZE    ", v);
    p_glGetIntegerv(0x8872, &v); kv("MAX_TEXTURE_IMAGE_UNITS      ", v);
    p_glGetIntegerv(0x8b4c, &v); kv("MAX_VERTEX_TEXTURE_UNITS     ", v);
    p_glGetIntegerv(0x8b4d, &v); kv("MAX_COMBINED_TEXTURE_UNITS   ", v);
    p_glGetIntegerv(0x8dfb, &v); kv("MAX_VERTEX_UNIFORM_VECTORS   ", v);
    p_glGetIntegerv(0x8dfc, &v); kv("MAX_VARYING_VECTORS          ", v);
    p_glGetIntegerv(0x8dfd, &v); kv("MAX_FRAGMENT_UNIFORM_VECTORS ", v);
    p_glGetIntegerv(0x8869, &v); kv("MAX_VERTEX_ATTRIBS           ", v);
    p_glGetIntegerv(0x84e8, &v); kv("MAX_RENDERBUFFER_SIZE        ", v);
    p_glGetIntegerv(0x0d50, &v); kv("SUBPIXEL_BITS                ", v);
    p_glGetIntegerv(0x0d52, &v); kv("RED_BITS                     ", v);
    p_glGetIntegerv(0x0d53, &v); kv("GREEN_BITS                   ", v);
    p_glGetIntegerv(0x0d54, &v); kv("BLUE_BITS                    ", v);
    p_glGetIntegerv(0x0d55, &v); kv("ALPHA_BITS                   ", v);
    p_glGetIntegerv(0x80a9, &v); kv("SAMPLES                      ", v);

    out("\n--- shader 精度 [最小指数,最大指数] 尾数位 ---\n");
    if (p_glGetShaderPrecisionFormat) {
        U sht[2]; U prt[6];
        I a, b;
        sht[0] = 0x8B30; sht[1] = 0x8B31;
        prt[0] = 0x8DF0; prt[1] = 0x8DF1; prt[2] = 0x8DF2;
        prt[3] = 0x8DF3; prt[4] = 0x8DF4; prt[5] = 0x8DF5;
        for (a = 0; a < 2; a++) {
            for (b = 0; b < 6; b++) {
                I rg[2];
                I prec = 0;
                char *p = buf;
                rg[0] = 0; rg[1] = 0;
                p_glGetShaderPrecisionFormat(sht[a], prt[b], rg, &prec);
                p = puts_(p, a ? "vert " : "frag ");
                p = putu(p, (U)b);
                p = puts_(p, " [");
                p = putu(p, (U)rg[0]);
                p = puts_(p, ",");
                p = putu(p, (U)rg[1]);
                p = puts_(p, "] bits=");
                p = putu(p, (U)prec);
                *p++ = 10;
                emit(p);
            }
        }
        out("  (b: 0=low_float 1=medium_float 2=high_float 3=low_int 4=medium_int 5=high_int)\n");
    } else {
        out("  glGetShaderPrecisionFormat 不可用\n");
    }

    out("\nNOTE: 只查询，未创建纹理/未绘制\n");
    return 0;
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #248\n"
        "svc 0\n");
