/* nx_afknob.c — NX500 AF 跟踪灵敏度三参数（ng / ok / small）的受控读写
 *
 * 依据 docs/current/AF_TRIGGER_AND_PATCH_2026-11-17.md §1（纯静态逆向）：
 *   GAF 基址 = 0x81550220，三个 int 相邻：
 *     0x8155044C  GAF+0x44  ng     默认 8   连续 NG 几次停止跟踪
 *     0x81550450  GAF+0x48  small  默认 4   小目标抖动次数
 *     0x81550454  GAF+0x4C  ok     默认 4   连续 OK 几次开始跟踪
 *   写入者：构造函数 0x8029B4E4（mov r2,#8 / mov r3,#4），
 *           唯一调用者 0x802AEC80 = AF 管理器一次性构造入口。
 *   ⇒ 构造期写一次，运行期只读。改写后不会被每帧覆盖。
 *
 * 物理可达性假设（本轮要验证的第一件事）：
 *   p7 的 DDR 与 Linux /dev/mem 的物理窗口重合。
 *   nx_regtabwrite 已实证同一片区的 0x80380000 可读可写（registry 页）。
 *
 * 安全设计（fail-closed，宁可不动）：
 *   1. 三个值必须同时可读且都落在 0..64，否则判 UNKNOWN 并拒绝写入
 *      （基址不对/页面被换掉时，读到的会是垃圾，不能拿来瞎写）
 *   2. set 要求当前值 == stock(8/4/4)，否则拒绝，除非显式 --force
 *   3. 写前整页快照落盘，写后逐字节对照：目标 12 字节之外有任何差异即 FAIL
 *   4. 目标值范围 0..64，越界拒绝
 *   5. 不持久化：p7 重建 AF 管理器或整机重启即回默认
 *
 * 用法：
 *   nx_afknob.arm status
 *   nx_afknob.arm set <ng> <ok> <small> [--force]
 *   nx_afknob.arm restore
 *   nx_afknob.arm snapshot <path>
 *
 * Build: arm-linux-gnueabi-gcc -nostdlib -static -O0 -marm -mfloat-abi=soft
 */
#include <asm/unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define PAGE_PA      0x81550000u
#define PAGE_SZ      0x1000u
#define OFF_NG       0x44Cu
#define OFF_SMALL    0x450u
#define OFF_OK       0x454u

#define DEF_NG       8
#define DEF_SMALL    4
#define DEF_OK       4

#define LIMIT        64          /* 三个值的合法上界（越大越粘，没有物理意义超过这个数） */
#define BAK_DIR      "/mnt/mmc/nxwa_afknob"
#define BAK_PATH     "/mnt/mmc/nxwa_afknob/gaf_page.bin"

static unsigned char snap[PAGE_SZ];

static int sc3(int n, int a, int b, int c)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory", "cc");
    return r0;
}

static int sc6(int n, int a, int b, int c, int d, int e, int f)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r3 __asm__("r3") = d;
    register int r4 __asm__("r4") = e;
    register int r5 __asm__("r5") = f;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4),
                     "r"(r5), "r"(r7) : "memory", "cc");
    return r0;
}

static int write_all(int fd, const void *buffer, unsigned size)
{
    const char *p = (const char *)buffer;
    while (size) {
        int n = sc3(__NR_write, fd, (int)p, (int)size);
        if (n == -4) continue;
        if (n <= 0) return -1;
        p += n;
        size -= (unsigned)n;
    }
    return 0;
}

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write_all(1, s, slen(s)); }
static int seq(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static char *put_hex(char *p, unsigned v)
{
    static const char d[] = "0123456789abcdef";
    int sh;
    *p++ = '0'; *p++ = 'x';
    for (sh = 28; sh >= 0; sh -= 4) *p++ = d[(v >> sh) & 15];
    return p;
}

static char *put_dec(char *p, int v)
{
    unsigned d[12], k = 0, j;
    if (v < 0) { *p++ = '-'; v = -v; }
    if (!v) d[k++] = 0;
    while (v) { d[k++] = (unsigned)(v % 10); v /= 10; }
    for (j = k; j > 0; --j) *p++ = (char)('0' + d[j - 1]);
    return p;
}

/* AFKNOB_<KEY>  <addr>  <dec>   —— 固定四列，PC 侧按空白切分 */
static void report(const char *key, unsigned addr, int v)
{
    char b[96], *p = b;
    const char *s;
    for (s = "AFKNOB_"; *s; ++s) *p++ = *s;
    for (s = key; *s; ++s) *p++ = *s;
    while ((unsigned)(p - b) < 18u) *p++ = ' ';
    p = put_hex(p, addr);
    *p++ = ' '; *p++ = ' ';
    p = put_dec(p, v);
    *p++ = '\n';
    write_all(1, b, (unsigned)(p - b));
}

static int parse_int(const char *s, int *out)
{
    int v = 0, any = 0, base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    while (*s) {
        int d;
        char c = *s;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * base + d;
        if (v > 100000) return -1;
        any = 1;
        ++s;
    }
    *out = v;
    return any ? 0 : -1;
}

/* 返回 0 成功，负数为错误码 */
static int map_page(void **addr, int writable, int *fd)
{
    int flags = writable ? (O_RDWR | O_SYNC) : (O_RDONLY | O_SYNC);
    int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    unsigned got;
    *fd = sc3(__NR_open, (int)"/dev/mem", flags, 0);
    if (*fd < 0) { say("AFKNOB_FAIL open /dev/mem\n"); return -1; }
    got = (unsigned)sc6(__NR_mmap2, 0, (int)PAGE_SZ, prot, MAP_SHARED, *fd,
                        (int)(PAGE_PA >> 12));
    if (got >= 0xFFFFF000u) {
        say("AFKNOB_FAIL mmap（该物理页可能被内核占着或不存在）\n");
        return -2;
    }
    *addr = (void *)got;
    return 0;
}

static void barrier(void)
{ __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory"); }

static int range_ok(unsigned v) { return v <= LIMIT; }

/* 整页内、目标 12 字节之外的差异计数 */
static unsigned stray_delta(const volatile unsigned char *live)
{
    unsigned i, stray = 0;
    for (i = 0; i < PAGE_SZ; ++i) {
        if (live[i] == snap[i]) continue;
        if (i >= OFF_NG && i < OFF_OK + 4u) continue;
        ++stray;
    }
    return stray;
}

static void snapshot(const volatile unsigned char *live)
{
    unsigned i;
    for (i = 0; i < PAGE_SZ; ++i) snap[i] = live[i];
}

static int save_page(const volatile unsigned char *live)
{
    int fd;
    sc3(__NR_mkdir, (int)BAK_DIR, 0755, 0);
    fd = sc3(__NR_open, (int)BAK_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { say("AFKNOB_WARN 备份落盘失败（不影响本次写入）\n"); return -1; }
    write_all(fd, (const void *)live, PAGE_SZ);
    sc3(__NR_close, fd, 0, 0);
    return 0;
}

static int nx_main(unsigned *sp) __attribute__((used));

static int nx_main(unsigned *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    volatile unsigned int *page;
    volatile unsigned int *slot;
    int fd, rc, stock;
    unsigned v_ng, v_small, v_ok;
    const char *cmd;

    if (argc < 2) {
        say("用法: nx_afknob.arm status | set <ng> <ok> <small> [--force] | restore | snapshot <path>\n");
        return 2;
    }
    cmd = argv[1];

    /* snapshot 用只读映射即可 */
    rc = map_page((void **)&page, 0, &fd);
    if (rc) return 3;
    snapshot((const volatile unsigned char *)page);

    slot = page + (OFF_NG / 4u);
    v_ng    = slot[0];
    v_small = slot[1];
    v_ok    = slot[2];

    report("NG",    PAGE_PA + OFF_NG,    v_ng);
    report("SMALL", PAGE_PA + OFF_SMALL, v_small);
    report("OK",    PAGE_PA + OFF_OK,    v_ok);

    if (!range_ok(v_ng) || !range_ok(v_small) || !range_ok(v_ok)) {
        say("AFKNOB_VERDICT UNKNOWN  三个值超出 0..64，基址或页面不对，拒绝任何写入\n");
        sc3(__NR_close, fd, 0, 0);
        return 4;
    }
    stock = (v_ng == DEF_NG && v_small == DEF_SMALL && v_ok == DEF_OK);
    say(stock ? "AFKNOB_VERDICT STOCK  当前是原厂默认 8/4/4\n"
              : "AFKNOB_VERDICT TUNED  当前不是原厂默认\n");

    if (seq(cmd, "status")) {
        sc3(__NR_close, fd, 0, 0);
        return 0;
    }

    if (seq(cmd, "snapshot")) {
        const char *path = (argc >= 3) ? argv[2] : 0;
        if (!path) { sc3(__NR_close, fd, 0, 0); say("AFKNOB_FAIL 需要路径\n"); return 2; }
        {
            int out = sc3(__NR_open, (int)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) { sc3(__NR_close, fd, 0, 0); say("AFKNOB_FAIL 打不开目标路径\n"); return 5; }
            write_all(out, (const void *)page, PAGE_SZ);
            sc3(__NR_close, out, 0, 0);
            say("AFKNOB_OK 整页已写出\n");
        }
        sc3(__NR_close, fd, 0, 0);
        return 0;
    }

    /* 以下两条需要可写映射：关掉只读映射重开 */
    sc3(__NR_close, fd, 0, 0);

    if (seq(cmd, "set")) {
        int want_ng, want_ok, want_small, force = 0;
        int i;
        if (argc < 5) { say("AFKNOB_FAIL set 需要三个数：set <ng> <ok> <small>\n"); return 2; }
        for (i = 4; i < argc; ++i) if (seq(argv[i], "--force")) force = 1;
        if (parse_int(argv[2], &want_ng) || parse_int(argv[3], &want_ok) ||
            parse_int(argv[4], &want_small)) {
            say("AFKNOB_FAIL 参数不是十进制/0x 整数\n");
            return 2;
        }
        if (!range_ok(want_ng) || !range_ok(want_ok) || !range_ok(want_small)) {
            say("AFKNOB_FAIL 目标值超出 0..64\n");
            return 2;
        }
        if (!stock && !force) {
            say("AFKNOB_FAIL 当前不是 8/4/4，不是第一次改（加 --force 才动）\n");
            return 6;
        }

        rc = map_page((void **)&page, 1, &fd);
        if (rc) return 3;
        slot = page + (OFF_NG / 4u);
        if (slot[0] != v_ng || slot[1] != v_small || slot[2] != v_ok) {
            say("AFKNOB_FAIL 两次读取之间值变了（AF 正在重建？）已放弃\n");
            sc3(__NR_close, fd, 0, 0);
            return 7;
        }
        save_page((const volatile unsigned char *)page);

        /* small 在中间，别按参数顺序写错位 */
        slot[0] = (unsigned)want_ng;
        slot[1] = (unsigned)want_small;
        slot[2] = (unsigned)want_ok;
        barrier();
        __asm__ volatile("" ::: "memory");

        report("READ_NG",    PAGE_PA + OFF_NG,    slot[0]);
        report("READ_SMALL", PAGE_PA + OFF_SMALL, slot[1]);
        report("READ_OK",    PAGE_PA + OFF_OK,    slot[2]);

        {
            unsigned stray = stray_delta((const volatile unsigned char *)page);
            if (stray) {
                char b[96], *p = b;
                const char *s;
                for (s = "AFKNOB_FAIL 页内目标之外有字节被改: "; *s; ++s) *p++ = *s;
                p = put_dec(p, (int)stray);
                *p++ = '\n';
                write_all(1, b, (unsigned)(p - b));
                sc3(__NR_close, fd, 0, 0);
                return 8;
            }
        }
        sc3(__NR_close, fd, 0, 0);

        if (slot[0] == (unsigned)want_ng && slot[1] == (unsigned)want_small &&
            slot[2] == (unsigned)want_ok) {
            say("AFKNOB_OK 写入并回读一致\n");
            return 0;
        }
        say("AFKNOB_FAIL 回读与写入不一致\n");
        return 9;
    }

    if (seq(cmd, "restore")) {
        rc = map_page((void **)&page, 1, &fd);
        if (rc) return 3;
        slot = page + (OFF_NG / 4u);
        slot[0] = DEF_NG;
        slot[1] = DEF_SMALL;
        slot[2] = DEF_OK;
        barrier();
        report("READ_NG",    PAGE_PA + OFF_NG,    slot[0]);
        report("READ_SMALL", PAGE_PA + OFF_SMALL, slot[1]);
        report("READ_OK",    PAGE_PA + OFF_OK,    slot[2]);
        sc3(__NR_close, fd, 0, 0);
        say("AFKNOB_OK 已写回 8/4/4\n");
        return 0;
    }

    say("AFKNOB_FAIL 未知命令\n");
    return 2;
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #1\n"
        "svc 0\n");
