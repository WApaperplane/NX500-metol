/* pwfast.c — FilmLab A2 档位池直写（8 条一次进程，无固定间隔）
 * =====================================================================
 * 为什么有它（2026-11-22 全部分项实测）：
 *   pwsend.arm 一次 seq 实测 ~400ms，拆开是
 *     进程启动 820KB(-O0)   ~60ms
 *     dlopen libcapture-fw  73ms
 *     mcb_init               2ms
 *     7×SetVariableDataMCB  0.4ms   ← 不是 16ms/次，是 0.05ms/次
 *     条间 7×20ms sleep     142ms
 *     收尾 usleep            100ms
 *   后两项共 242ms 是纯等待，且都不必要：
 *     · 条间间隔：svdm 是同步返回（rc=0 即已入 MCB 队列），队列本身 FIFO。
 *       2026-11-22 实测 sleep=0 发七维全改（112/111/108 12/8/9/10），
 *       varlist 读回 R/G/B=111/111/107 HUE=12 SAT=8 SHARP=9 CON=10 —— 七维全落地，
 *       末条提交位也生效，prefman slot9 全程未被写。恢复原值后逐维复现基线。
 *     · 收尾 usleep：preview 路径发完即退出、不读回；读回由 `filmlab.sh check`
 *       单独做（它自己会等 ISP）。
 *   本工具 -nostdlib + 原生 syscall（不链 glibc），体积 8KB，绕开相机老 glibc
 *   的版本依赖；退出用 exit_group(248) —— dlopen 的库会起线程，syscall 1 只退
 *   主线程，进程会变僵尸（实测踩过）。
 *
 * 用法：
 *   pwfast.arm <R> <G> <B> <HUE> <SAT> <SHARP> <CON>      # 静默，输出一行
 *   pwfast.arm -v <R> ... <CON>                            # 逐条耗时
 *
 * 值域：R/G/B 0..200（1.00 = 100），HUE/SAT/SHARP/CON 0..20（中性 = 10）。
 * 固定发送顺序 0x134..0x13A + 0x13B=1（提交位）。
 *
 * 构建：
 *   arm-linux-gnueabi-gcc -O2 -nostdlib -fno-builtin -fno-stack-protector \
 *     -Wall -Wextra -Werror -Wl,-e,_start -Wl,--allow-shlib-undefined \
 *     -o pwfast.arm pwfast.c syslib/libdl-2.13.so -lgcc
 * =====================================================================
 */
typedef unsigned int U;
typedef int I;
typedef void *P;

extern P dlopen(const char *, I);
extern P dlsym(P, const char *);

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

static char buf[320];

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

static char *puts_(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static U now_us(void)
{
    struct { I sec; I nsec; } ts;
    register I r0 __asm__("r0") = 1;            /* CLOCK_MONOTONIC */
    register void *r1 __asm__("r1") = &ts;
    register I r7 __asm__("r7") = 263;
    ts.sec = 0; ts.nsec = 0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return (U)ts.sec * 1000000u + (U)(ts.nsec / 1000u);
}

static U pnum(const char *s)
{
    U v = 0;
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (;;) {
            U d;
            if (*s >= '0' && *s <= '9') d = (U)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') d = (U)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') d = (U)(*s - 'A' + 10);
            else break;
            v = v * 16u + d;
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') { v = v * 10u + (U)(*s - '0'); s++; }
    }
    return v;
}

typedef int (*fn_svdm)(unsigned short id, void *data, unsigned short len);

I nx_main(U *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    static const unsigned short ids[8] = {
        0x134, 0x135, 0x136, 0x137, 0x138, 0x139, 0x13A, 0x13B
    };
    int vals[8];
    int verbose = 0;
    int base = 1;
    int i, bad = 0;
    U t0, t1;
    P hcap, hmcb;
    fn_svdm svdm;
    int (*p_init)(void);
    void (*p_ignore)(void);
    char *p;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'v') { verbose = 1; base = 2; }
    if (argc < base + 7) {
        out("usage: pwfast.arm [-v] R G B HUE SAT SHARP CON\n");
        return 1;
    }
    for (i = 0; i < 7; i++) {
        U v = pnum(argv[base + i]);
        if (v > 200) {
            p = buf; p = puts_(p, "pwfast: value out of range: "); p = putu(p, v); p = puts_(p, "\n");
            emit(p);
            return 1;
        }
        vals[i] = (int)v;
    }
    vals[7] = 1;                                   /* 提交位 */

    t0 = now_us();
    hcap = dlopen("libcapture-fw-prod.so", 1);
    if (!hcap) hcap = dlopen("/usr/lib/libcapture-fw-prod.so", 1);
    if (!hcap) { out("pwfast: FATAL dlopen libcapture-fw-prod\n"); return 2; }
    svdm = (fn_svdm)dlsym(hcap, "SetVariableDataMCB");
    if (!svdm) { out("pwfast: FATAL dlsym SetVariableDataMCB\n"); return 3; }

    hmcb = dlopen("libmulticore-bridge.so", 1);
    if (!hmcb) hmcb = dlopen("/usr/lib/libmulticore-bridge.so", 1);
    if (!hmcb) { out("pwfast: FATAL dlopen libmulticore-bridge\n"); return 4; }
    p_init   = (int (*)(void))dlsym(hmcb, "mcb_init");
    p_ignore = (void (*)(void))dlsym(hmcb, "mcb_set_ignore_init_checking");
    if (!p_init) { out("pwfast: FATAL dlsym mcb_init\n"); return 5; }
    p_init();
    if (p_ignore) p_ignore();
    t1 = now_us();

    for (i = 0; i < 8; i++) {
        int v = vals[i];
        int rc = svdm(ids[i], &v, 4);
        if (rc != 0) {
            bad = i + 1;
            if (verbose) {
                p = buf; p = puts_(p, "pwfast: send "); p = putu(p, (U)i);
                p = puts_(p, " rc="); p = putu(p, (U)rc); p = puts_(p, "\n"); emit(p);
            }
        }
    }

    p = buf;
    if (bad) {
        p = puts_(p, "pwfast: FAIL at #");
        p = putu(p, (U)(bad - 1));
        p = puts_(p, " id=");
        p = putu(p, (U)ids[bad - 1]);
        p = puts_(p, "\n");
    } else {
        p = puts_(p, "pwfast: 8/8 ok ");
        p = putu(p, (now_us() - t0) / 1000u);
        p = puts_(p, "ms (init ");
        p = putu(p, (t1 - t0) / 1000u);
        p = puts_(p, "ms)\n");
    }
    emit(p);
    return bad ? 6 : 0;
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #248\n"        /* exit_group */
        "svc 0\n");
