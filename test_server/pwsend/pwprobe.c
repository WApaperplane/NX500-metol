/* pwprobe.c — pwsend 发送链路的进程内分段计时（NX500 / NX-KS2）
 * =====================================================================
 * 目的：把 `pw_direct` 那 ~400ms 拆开。sh 侧只能看到"起了个进程用了多久"，
 *   dlopen / mcb_init / 每次 svdm 各花多少必须进程内测。
 *
 * 安全：本探针**不调用 SetVariableDataMCB**，只做动态加载与 MCB 桥初始化。
 *   MCB 桥初始化在 di-camera-app 里是幂等且已完成的（同一进程），
 *   本进程只是另起一个句柄，不改 ISP 参数、不切档位。
 *   加 --send 才发一条（默认不发）。
 *
 * 构建（与 gpu_bench 同路子，避开相机老 glibc）：
 *   arm-linux-gnueabi-gcc -O2 -nostdlib -fno-builtin -fno-stack-protector \
 *     -Wall -Wextra -Werror -Wl,-e,_start -Wl,--allow-shlib-undefined \
 *     -o pwprobe.arm pwprobe.c syslib/libdl-2.13.so -lgcc
 *
 * 用法：
 *   ./pwprobe.arm            # 只测加载与桥初始化
 *   ./pwprobe.arm 3          # 同一桥句柄上连做 3 轮"空转"（测常驻的边际成本）
 */
typedef unsigned int U;
typedef int I;
typedef void *P;

extern P dlopen(const char *, I);
extern P dlsym(P, const char *);
extern char *dlerror(void);

/* ---------------- 输出 / syscall（照 gpu_bench，避免链 libc） ---------------- */
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

static void emit(char *b, char *p)
{
    char saved = *p;
    *p = 0;
    out(b);
    *p = saved;
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

/* 无 libc 的数字解析：十进制或 0x 前缀十六进制 */
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

/* 微秒级单调时钟：32 位够单进程 71 分钟，避免 64 位除法拖进 libgcc */
static U now_us(void)
{
    struct { I sec; I nsec; } ts;
    register I r0 __asm__("r0") = 1;            /* CLOCK_MONOTONIC */
    register void *r1 __asm__("r1") = &ts;
    register I r7 __asm__("r7") = 263;          /* clock_gettime */
    ts.sec = 0; ts.nsec = 0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return (U)ts.sec * 1000000u + (U)(ts.nsec / 1000);
}

static void sleep_ms(U ms)
{
    struct { I sec; I nsec; } ts, rem;
    /* nanosleep(req, rem)：r0 必须是 req 指针。★ 曾把 r0 置 0 → 恒 EFAULT 立即返回，
     * 于是"sleep 20ms"被静默跳过，测出来的 WALL 是纯工作成本。 */
    register void *r0 __asm__("r0") = &ts;
    register void *r1 __asm__("r1") = &rem;
    register I r7 __asm__("r7") = 162;          /* nanosleep */
    ts.sec = (I)(ms / 1000u);
    ts.nsec = (I)((ms % 1000u) * 1000000u);
    rem.sec = 0; rem.nsec = 0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
}

static char buf[512];

static void say(const char *label, U us)
{
    char *p = buf;
    p = puts_(p, label);
    p = puts_(p, " = ");
    p = putu(p, us / 1000u);
    p = puts_(p, ".");
    /* 三位小数毫秒：必须逐位补零，100+frac 在 frac>899 时会进位成四位 */
    {
        U frac = us % 1000u;
        *p++ = (char)('0' + (frac / 100u) % 10u);
        *p++ = (char)('0' + (frac / 10u) % 10u);
        *p++ = (char)('0' + frac % 10u);
    }
    p = puts_(p, " ms\n");
    emit(buf, p);
}

static void sayn(const char *label, U v)
{
    char *p = buf;
    p = puts_(p, label);
    p = puts_(p, " = ");
    p = putu(p, v);
    p = puts_(p, "\n");
    emit(buf, p);
}

typedef int (*fn_svdm)(unsigned short id, void *data, unsigned short len);
typedef int (*fn_sudm)(unsigned short id, void *data, unsigned short len);

static I do_send(int argc, char **argv);
static I do_setusr(int argc, char **argv);

I nx_main(U *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);

    if (argc > 1 && argv[1][0] == 's' && argv[1][1] == 'e' && argv[1][2] == 'n')
        return do_send(argc, argv);
    if (argc > 1 && argv[1][0] == 'u' && argv[1][1] == 's')
        return do_setusr(argc, argv);

    {
    U t0, t1, t2, t3, t4, t5, t6;
    P hcap, hmcb;
    fn_svdm svdm;
    int (*p_init)(void);
    int (*p_alive)(void);
    void (*p_ignore)(void);
    int rounds = 1;
    int i;

    if (argc > 1) {
        /* 只接受 1..5 的小整数轮数 */
        char *a = argv[1];
        if (a[0] >= '1' && a[0] <= '5' && a[1] == 0) rounds = a[0] - '0';
    }

    t0 = now_us();

    hcap = dlopen("libcapture-fw-prod.so", 1 /* RTLD_LAZY */);
    if (!hcap) hcap = dlopen("/usr/lib/libcapture-fw-prod.so", 1);
    t1 = now_us();
    if (!hcap) {
        out("FATAL dlopen(libcapture-fw-prod) failed: ");
        out(dlerror()); out("\n");
        return 2;
    }
    svdm = (fn_svdm)dlsym(hcap, "SetVariableDataMCB");
    t2 = now_us();
    if (!svdm) { out("FATAL dlsym(SetVariableDataMCB) failed\n"); return 3; }

    hmcb = dlopen("libmulticore-bridge.so", 1);
    if (!hmcb) hmcb = dlopen("/usr/lib/libmulticore-bridge.so", 1);
    t3 = now_us();
    if (!hmcb) { out("FATAL dlopen(libmulticore-bridge) failed\n"); return 4; }

    p_init  = (int (*)(void))dlsym(hmcb, "mcb_init");
    p_alive = (int (*)(void))dlsym(hmcb, "_ZN16CMulticoreBridge18IsDestinationAliveEv");
    p_ignore = (void (*)(void))dlsym(hmcb, "mcb_set_ignore_init_checking");
    if (!p_init) { out("FATAL dlsym(mcb_init) failed\n"); return 5; }

    p_init();
    t4 = now_us();

    if (p_ignore) p_ignore();
    t5 = now_us();

    /* IsDestinationAlive：pwsend 里最多轮 40 次 × 100ms。这里记录真实轮数 */
    {
        int polls = 0;
        int alive = -1;
        if (p_alive) {
            for (i = 0; i < 40; i++) {
                alive = p_alive();
                polls++;
                if (alive) break;
                sleep_ms(100);
            }
        }
        t6 = now_us();
        sayn("polls", (U)polls);
        sayn("destAlive", (U)alive);
    }

    say("t0->dlopen(cap)       ", t1 - t0);
    say("dlopen(cap)->dlsym    ", t2 - t1);
    say("dlsym->dlopen(mcb)    ", t3 - t2);
    say("dlopen(mcb)->mcb_init ", t4 - t3);
    say("mcb_init->set_ignore  ", t5 - t4);
    say("set_ignore->alive     ", t6 - t5);
    say("TOTAL init            ", t6 - t0);

    /* 常驻形态下"再发一轮"的边际成本：同一句柄重复建链（幂等） */
    if (rounds > 1) {
        U r0, r1;
        r0 = now_us();
        for (i = 1; i < rounds; i++) p_init();
        r1 = now_us();
        say("extra mcb_init 1 round", (r1 - r0));
    }

    (void)svdm;   /* 默认不发送 */
    out("NOTE: no SetVariableDataMCB call was made\n");
    return 0;
    }
}

/* ---------------------------------------------------------------------------
 * send <R> <G> <B> <HUE> <SAT> <SHARP> <CON> [sleep_ms] [tail_ms]
 *   固定 8 条：0x134..0x13A 七维 + 0x13B 提交位。
 *   发与原厂/槽位相同的值时画面不变；运行时档位已在 CUSTOM1 时 0x13B 也无感。
 *   默认 sleep=20（= filmlab 浏览路径现值）、tail=0（pwsend 里是 100ms）。
 * ------------------------------------------------------------------------- */
static I do_send(int argc, char **argv)
{
    static const unsigned short ids[8] = {
        0x134, 0x135, 0x136, 0x137, 0x138, 0x139, 0x13A, 0x13B
    };
    int vals[8];
    I rc[8];
    U each[8];
    U slp = 20, tail = 0;
    U t_start, t_dl, t_mcb, t_send;
    P hcap, hmcb;
    fn_svdm svdm;
    int (*p_init)(void);
    void (*p_ignore)(void);
    int i, bad = 0;

    if (argc < 9) {
        out("usage: pwprobe send R G B HUE SAT SHARP CON [sleep_ms] [tail_ms]\n");
        return 1;
    }
    for (i = 0; i < 7; i++) {
        U v = pnum(argv[2 + i]);
        if (v > 200) { out("FATAL value out of range (0..200)\n"); return 1; }
        vals[i] = (int)v;
    }
    vals[7] = 1;                                    /* 提交位 */
    if (argc > 9)  slp  = pnum(argv[9]);
    if (argc > 10) tail = pnum(argv[10]);
    if (slp  > 2000) slp  = 2000;
    if (tail > 2000) tail = 2000;

    t_start = now_us();
    hcap = dlopen("libcapture-fw-prod.so", 1);
    if (!hcap) hcap = dlopen("/usr/lib/libcapture-fw-prod.so", 1);
    t_dl = now_us();
    if (!hcap) { out("FATAL dlopen libcapture-fw-prod failed\n"); return 2; }
    svdm = (fn_svdm)dlsym(hcap, "SetVariableDataMCB");
    if (!svdm) { out("FATAL dlsym(SetVariableDataMCB) failed\n"); return 3; }

    hmcb = dlopen("libmulticore-bridge.so", 1);
    if (!hmcb) hmcb = dlopen("/usr/lib/libmulticore-bridge.so", 1);
    if (!hmcb) { out("FATAL dlopen libmulticore-bridge failed\n"); return 4; }
    p_init   = (int (*)(void))dlsym(hmcb, "mcb_init");
    p_ignore = (void (*)(void))dlsym(hmcb, "mcb_set_ignore_init_checking");
    if (!p_init) { out("FATAL dlsym(mcb_init) failed\n"); return 5; }
    p_init();
    if (p_ignore) p_ignore();
    t_mcb = now_us();

    for (i = 0; i < 8; i++) {
        U a = now_us();
        int v = vals[i];
        rc[i] = svdm(ids[i], &v, 4);
        each[i] = now_us() - a;
        if (rc[i] != 0) bad++;
        if (i + 1 < 8 && slp) sleep_ms(slp);
    }
    t_send = now_us();
    if (tail) sleep_ms(tail);

    {
        char *p = buf;
        p = puts_(p, "dlopen     ");
        p = putu(p, (t_dl - t_start) / 1000u);
        p = puts_(p, " ms\nmcb init   ");
        p = putu(p, (t_mcb - t_dl) / 1000u);
        p = puts_(p, " ms\n");
        emit(buf, p);
    }
    for (i = 0; i < 8; i++) {
        char *p = buf;
        p = puts_(p, "svdm[");
        p = putu(p, (U)i);
        p = puts_(p, "] 0x1");
        p = putu(p, (U)((ids[i] >> 4) & 0xF));
        p = putu(p, (U)(ids[i] & 0xF));
        p = puts_(p, " <- ");
        p = putu(p, (U)vals[i]);
        p = puts_(p, "   ");
        p = putu(p, each[i] / 1000u);
        p = puts_(p, " ms rc=");
        p = putu(p, (U)rc[i]);
        p = puts_(p, "\n");
        emit(buf, p);
    }
    say("8 svdm total", t_send - t_mcb);
    sayn("sleep per gap", slp);
    sayn("tail wait   ", tail);
    say("WALL total  ", now_us() - t_start);
    if (bad) { out("NOTE: some svdm returned non-zero\n"); return 5; }
    return 0;
}

/* ---------------------------------------------------------------------------
 * uset <id> <value>
 *   调 SetUserDataMCB —— 与 `st cap capdtm setusr <id> <value>` 同族的库入口。
 *   目的：验证能否绕开 st（83 个库 / 340ms）直接写 CAP DTM 的 userdata。
 *   第 3 参按 SetVariableDataMCB 的同族签名传 4；若它实际只吃 2 个参数，多余的在 r2 被忽略。
 * ------------------------------------------------------------------------- */
static I do_setusr(int argc, char **argv)
{
    P hcap, hmcb;
    fn_sudm sudm;
    int (*p_init)(void);
    void (*p_ignore)(void);
    int id, v, rc;
    U t0, t1, t2;

    if (argc < 4) { out("usage: pwprobe uset <id> <value>\n"); return 1; }
    id = (int)pnum(argv[2]);
    v  = (int)pnum(argv[3]);

    t0 = now_us();
    hcap = dlopen("libcapture-fw-prod.so", 1);
    if (!hcap) hcap = dlopen("/usr/lib/libcapture-fw-prod.so", 1);
    if (!hcap) { out("FATAL dlopen libcapture-fw-prod failed\n"); return 2; }
    sudm = (fn_sudm)dlsym(hcap, "SetUserDataMCB");
    if (!sudm) { out("FATAL dlsym(SetUserDataMCB) failed\n"); return 3; }
    t1 = now_us();

    hmcb = dlopen("libmulticore-bridge.so", 1);
    if (!hmcb) hmcb = dlopen("/usr/lib/libmulticore-bridge.so", 1);
    if (!hmcb) { out("FATAL dlopen libmulticore-bridge failed\n"); return 4; }
    p_init   = (int (*)(void))dlsym(hmcb, "mcb_init");
    p_ignore = (void (*)(void))dlsym(hmcb, "mcb_set_ignore_init_checking");
    if (p_init) p_init();
    if (p_ignore) p_ignore();
    t2 = now_us();

    {
        char *p = buf;
        U vv = (U)v;
        int sh, started = 0;
        p = puts_(p, "uset id=");
        p = putu(p, (U)id);
        p = puts_(p, " val=0x");
        for (sh = 28; sh >= 0; sh -= 4) {
            U d = (vv >> sh) & 0xFu;
            if (d || started || sh == 0) {
                *p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                started = 1;
            }
        }
        p = puts_(p, "\n");
        emit(buf, p);
    }

    rc = sudm((unsigned short)id, &v, 4);
    say("dlopen cap   ", t1 - t0);
    say("mcb init     ", t2 - t1);
    sayn("rc           ", (U)rc);
    return rc == 0 ? 0 : 5;
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #248\n"        /* exit_group：dlopen 的库会起线程，syscall 1(exit) 只退主线程 */
        "svc 0\n");
