/* capq.c — 直接向 CAP DTM 的 shell 消息队列投递命令（绕开 st 的 83 库加载）
 * =====================================================================
 * 背景
 *   `st cap capdtm getusr 20` 实测 340~430ms，其中 user 0.29s 全是 83 个共享库的
 *   重定位与构造函数，与"读什么"无关。逆向 libshell-command.so 的 shell_sndmsg
 *   （0x7470）发现 cap 路径根本是 System V 消息队列，全部常量如下：
 *
 *     msgget(key = 34856, 0666)          ← key 由 st 的 main 以常量传入（0x8828）
 *     消息 208 B：+0 mtype=1, +4 argc, +8 起每参数 20 B（strncpy，含 NUL）
 *     sem_open("/sem_app_res", O_CREAT, 0640, 0) → sem_wait
 *     结果：读 /tmp/app_result（服务端写入后 sem_post）
 *
 *   即"发一条消息、等信号量、读一个文本文件"。本工具就干这三件事，不链任何
 *   Tizen 库；dlopen libc/librt 只为拿 msgget/msgsnd/sem_*（libc 随 libdl 已在内存）。
 *
 * 安全
 *   · 默认 dry-run：只打印将发送的 208 B 布局与队列状态，不发消息。
 *   · --yes 才真发。超时用 sem_timedwait（3s），绝不无限阻塞。
 *   · 只读命令（getusr / usrlist）风险为零；setusr 会改相机状态，本工具不禁止，
 *     但调用方要自己清楚。
 *
 * 构建（与 pwfast 同路子，避开相机老 glibc）：
 *   arm-linux-gnueabi-gcc -O2 -nostdlib -fno-builtin -fno-stack-protector \
 *     -Wl,-e,_start -Wl,--allow-shlib-undefined -o capq.arm capq.c \
 *     ../gpu/syslib/libdl-2.13.so -lgcc
 * =====================================================================
 */
typedef unsigned int U;
typedef int I;
typedef void *P;
typedef long L;

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

static char buf[512];

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

/* ★ 必须两个都自增。最初这里写成 `while (*s) *p++ = *s;` —— 漏了 s++，
 *   结果把首字符写了 4096 次，越过页界段错误（内核栈里 r3=0x12000 而 r2 仍是 'c'）。 */
static char *puts_(char *p, const char *s)
{
    while (*s) {
        *p = *s;
        p++;
        s++;
    }
    return p;
}

static void kv(const char *k, I v)
{
    char t[16];
    char *p = putu(t, (U)v);
    *p = 0;
    out(k);
    out(t);
    out("\n");
}


/* ---- System V IPC 走原生 syscall ----
 * ★ 不能调 glibc 的 msgget/msgsnd：-nostdlib 进程没走 __libc_start_main，
 *   glibc 的 TLS 与堆未初始化，实测 free(): invalid next size 后段错误。
 *   ★ 这不是标准 ARM 号：三星内核把 IPC 重编到 __NR_semget=299 /
 *   msgsnd=301 / msgrcv=302 / msgget=303（见固件 standard-armv7l/usr/include/
 *   asm/unistd.h）。用标准的 395/396 会得到 -38 ENOSYS（实测）。 */
static I nx_msgget(I key, I flags)
{
    register I r0 __asm__("r0") = key;
    register I r1 __asm__("r1") = flags;
    register I r7 __asm__("r7") = 303;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}

static I nx_msgsnd(I qid, const void *msgp, U sz, I flags)
{
    register I r0 __asm__("r0") = qid;
    register const void *r1 __asm__("r1") = msgp;
    register U r2 __asm__("r2") = sz;
    register I r3 __asm__("r3") = flags;
    register I r7 __asm__("r7") = 301;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory");
    return r0;
}

/* 只取 st_mtime / st_mtime_nsec（ARM EABI stat64 里在偏移 72 / 76）。
 * 返回 1 表示拿到了时间戳，0 表示文件不存在。 */
static char statb[128];
static I file_mtime(I fd, U *sec, U *nsec)
{
    register I r0 __asm__("r0") = fd;
    register void *r1 __asm__("r1") = statb;
    register I r7 __asm__("r7") = 197;          /* fstat64 */
    I rc;
    int i;
    for (i = 0; i < 128; i++) statb[i] = 0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    rc = r0;
    if (rc != 0) return 0;
    *sec = *(U *)(statb + 72);
    *nsec = *(U *)(statb + 76);
    return 1;
}

/* 消息体 208 B：mtype(4) + argc(4) + 10×20 B 参数 */
static char msgb[208];

/* open/read/close 走原生 syscall，省一次 dlopen */
static I nx_open(const char *path, I flags)
{
    register I r0 __asm__("r0") = -100;          /* AT_FDCWD */
    register const char *r1 __asm__("r1") = path;
    register I r2 __asm__("r2") = flags;
    register I r3 __asm__("r3") = 0;
    register I r7 __asm__("r7") = 322;           /* openat */
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory");
    return r0;
}

static I nx_read(I fd, void *b, U n)
{
    register I r0 __asm__("r0") = fd;
    register void *r1 __asm__("r1") = b;
    register U r2 __asm__("r2") = n;
    register I r7 __asm__("r7") = 3;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static I nx_close(I fd)
{
    register I r0 __asm__("r0") = fd;
    register I r7 __asm__("r7") = 6;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static void nx_msleep(U ms)
{
    struct { I sec; I nsec; } ts, rem;
    register void *r0 __asm__("r0") = &ts;
    register void *r1 __asm__("r1") = &rem;
    register I r7 __asm__("r7") = 162;
    ts.sec = (I)(ms / 1000u);
    ts.nsec = (I)((ms % 1000u) * 1000000u);
    rem.sec = 0; rem.nsec = 0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
}

I nx_main(U *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    int yes = 0, i, j, narg = 0;
    int key = 34856;
    const char *rpath = "/tmp/app_result";
    I qid, fd;
    U old_s = 0, old_n = 0, new_s, new_n;
    int have_old = 0;

    /* --yes 是本工具自己的开关，绝不能进消息：st 发的是纯命令参数 */
    if (argc > 1) {
        char *a = argv[argc - 1];
        if (a[0] == '-' && a[1] == '-') { yes = 1; argc--; }
    }

    for (i = 0; i < 208; i++) msgb[i] = 0;
    *(I *)(msgb + 0) = 1;                        /* mtype */
    narg = (argc > 10) ? 10 : argc;
    *(I *)(msgb + 4) = narg;
    for (i = 0; i < narg; i++) {
        char *dst = msgb + 8 + i * 20;
        const char *src = (i == 0) ? "st" : argv[i];   /* 服务端看到的首参是 st */
        for (j = 0; j < 19 && src[j]; j++) dst[j] = src[j];
        dst[j] = 0;
    }

    {
        char *p = buf;
        p = puts_(p, "capq: key=");
        p = putu(p, (U)key);
        p = puts_(p, " argc=");
        p = putu(p, (U)narg);
        p = puts_(p, " mode=");
        p = puts_(p, yes ? "SEND" : "dry-run");
        p = puts_(p, "\n");
        emit(p);
    }
    for (i = 0; i < narg; i++) {
        char *p = buf;
        p = puts_(p, "  [");
        p = putu(p, (U)i);
        p = puts_(p, "] ");
        p = puts_(p, msgb + 8 + i * 20);
        p = puts_(p, "\n");
        emit(p);
    }

    qid = nx_msgget(key, 0666);
    if (!yes) {
        kv("  msgget(0666) -> ", qid);
        out("  （未发送；加 --yes 才真发）\n");
        return 0;
    }
    if (qid < 0) { out("FATAL msgget failed（队列不存在）\n"); return 4; }
    kv("  qid -> ", qid);

    /* 记旧 mtime，用来判断服务端是否真的重写了结果文件 */
    fd = nx_open(rpath, 0);
    if (fd >= 0) {
        have_old = file_mtime(fd, &old_s, &old_n);
        nx_close(fd);
    }

    if (nx_msgsnd(qid, msgb, 208, 0) != 0) { out("FATAL msgsnd failed\n"); return 5; }
    out("  msgsnd ok\n");

    {
        int got = 0, tries;
        for (tries = 0; tries < 100; tries++) {
            fd = nx_open(rpath, 0);
            if (fd >= 0) {
                if (file_mtime(fd, &new_s, &new_n)) {
                    if (!have_old || new_s != old_s || new_n != old_n) {
                        got = 1;
                        nx_close(fd);
                        break;
                    }
                }
                nx_close(fd);
            }
            nx_msleep(10);
        }
        if (got) out("  result updated\n");
        else out("WARN 2s 内未见结果文件更新（值可能未变，仍按当前内容显示）\n");
    }

    fd = nx_open(rpath, 0);
    if (fd < 0) { out("FATAL open /tmp/app_result failed\n"); return 7; }
    out("---- /tmp/app_result ----\n");
    for (;;) {
        char rb[256];
        I n = nx_read(fd, rb, sizeof rb - 1);
        if (n <= 0) break;
        rb[n] = 0;
        out(rb);
    }
    nx_close(fd);
    out("\n---- end ----\n");
    return 0;
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #248\n"
        "svc 0\n");
