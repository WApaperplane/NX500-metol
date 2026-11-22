/* nxthumb - slice the preview images an NX500 JPEG already carries.
 *
 * The camera writes two derived images into every still:
 *   - EXIF IFD1 thumbnail, 320x216, sits inside the first 64 KB
 *   - MPF appended image, 1616x1080, ends the file
 * Both are complete JPEG streams. Handing them out costs one small read and
 * no decode, where ImageMagick needs seconds and tens of megabytes.
 *
 * Pixels are never rotated by the camera; orientation only exists as an EXIF
 * tag. A sliced 320x216 frame from a portrait shot is therefore sideways, so
 * when orientation != 1 a 36-byte APP1 carrying just that tag is prepended and
 * the browser rotates it (image-orientation: from-image is the default).
 *
 * Raw syscalls only: the shipped glibc is 2.13 and statically linking a modern
 * one would drag in syscalls this 3.5 kernel does not have.
 *
 * usage:
 *   nxthumb thumb <file>   EXIF grid thumbnail to stdout
 *   nxthumb big   <file>   MPF appended image to stdout
 *   nxthumb info  <file>   "thumblen orient biglen mainw mainh thumbw thumbh"
 *
 * exit: 0 ok, 1 usage, 2 nothing usable inside
 */

#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_lseek 19

#define O_RDONLY 0
#define SEEK_SET 0
#define SEEK_END 2

#define HEAD_MAX 163840 /* covers a 60 KB APP1 plus the thumbnail it indexes */
#define WIN_MAX 1024
#define IOBUF 8192

static inline int sys1(int n, int a) {
    register int r0 __asm__("r0") = a;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}
static inline int sys2(int n, int a, int b) {
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}
static inline int sys3(int n, int a, int b, int c) {
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static int xopen(const char *p) { return sys3(SYS_open, (int)p, O_RDONLY, 0); }
static int xread(int fd, void *b, int n) { return sys3(SYS_read, fd, (int)b, n); }
static int xseek(int fd, int off, int whence) { return sys3(SYS_lseek, fd, off, whence); }
static void xclose(int fd) { sys1(SYS_close, fd); }
static void xexit(int c) __attribute__((noreturn));
static void xexit(int c) {
    sys1(SYS_exit, c);
    for (;;) {
    }
}

static int xwrite(int fd, const void *b, int n) {
    const char *p = (const char *)b;
    int left = n;
    while (left > 0) {
        int w = sys3(SYS_write, fd, (int)p, left);
        if (w <= 0) return -1;
        p += w;
        left -= w;
    }
    return n;
}

static void out(const char *s) {
    int n = 0;
    while (s[n]) n++;
    xwrite(1, s, n);
}
static void outdec(unsigned v) {
    char b[12];
    int i = 0;
    if (!v) b[i++] = '0';
    while (v) {
        b[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--) xwrite(1, &b[i], 1);
}

static int meq(const unsigned char *a, const char *b, int n) {
    int i;
    for (i = 0; i < n; i++)
        if (a[i] != (unsigned char)b[i]) return 0;
    return 1;
}
static void mzero(void *p, int n) {
    unsigned char *q = (unsigned char *)p;
    while (n-- > 0) *q++ = 0;
}
static void mcopy(void *d, const void *s, int n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n-- > 0) *a++ = *b++;
}

static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}
static unsigned le16(const unsigned char *p) { return ((unsigned)p[1] << 8) | p[0]; }
static unsigned le32(const unsigned char *p) {
    return ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) | ((unsigned)p[1] << 8) | p[0];
}

typedef struct {
    unsigned orientation; /* 1 when the tag is absent */
    unsigned thumb_off;   /* absolute file offset, 0 when absent */
    unsigned thumb_len;
    unsigned thumb_w, thumb_h;
    unsigned big_off; /* absolute file offset, 0 when absent */
    unsigned big_len;
    unsigned main_w, main_h;
} probe_t;

static unsigned rd16(const unsigned char *t, int big, unsigned o) {
    return big ? be16(t + o) : le16(t + o);
}
static unsigned rd32(const unsigned char *t, int big, unsigned o) {
    return big ? be32(t + o) : le32(t + o);
}

/* Walk IFD0 for orientation, then IFD1 for the thumbnail's offset and length.
 * `lim` bounds every read so a corrupt count cannot drive us past the buffer. */
static void parse_tiff(const unsigned char *tiff, unsigned lim, unsigned abs_base, probe_t *r) {
    int big;
    unsigned ifd0, n, k, next;
    if (lim < 8) return;
    if (tiff[0] == 'M' && tiff[1] == 'M') big = 1;
    else if (tiff[0] == 'I' && tiff[1] == 'I') big = 0;
    else return;
    ifd0 = rd32(tiff, big, 4);
    if (ifd0 + 2 > lim) return;
    n = rd16(tiff, big, ifd0);
    if (n > 512 || ifd0 + 2 + n * 12 + 4 > lim) return;
    for (k = 0; k < n; k++) {
        unsigned e = ifd0 + 2 + k * 12;
        if (rd16(tiff, big, e) == 0x0112) r->orientation = rd16(tiff, big, e + 8);
    }
    next = rd32(tiff, big, ifd0 + 2 + n * 12);
    if (!next || next + 2 > lim) return;
    n = rd16(tiff, big, next);
    if (n > 512 || next + 2 + n * 12 > lim) return;
    {
        unsigned off = 0, len = 0;
        for (k = 0; k < n; k++) {
            unsigned e = next + 2 + k * 12;
            unsigned tag = rd16(tiff, big, e);
            if (tag == 0x0201) off = rd32(tiff, big, e + 8);
            else if (tag == 0x0202) len = rd32(tiff, big, e + 8);
            else if (tag == 0x0100) r->thumb_w = rd16(tiff, big, e + 8);
            else if (tag == 0x0101) r->thumb_h = rd16(tiff, big, e + 8);
        }
        if (off && len) {
            r->thumb_off = abs_base + off;
            r->thumb_len = len;
        }
    }
}

/* MP Entry: [attr 4][size 4][offset 4][dep1 2][dep2 2]. The offset is relative
 * to the MPF payload, but Samsung's writer leaves it 4 bytes short of the real
 * stream start, so only the size is trusted and the offset becomes a hint. */
static void parse_mpf(const unsigned char *mpf, unsigned lim, unsigned abs_base, probe_t *r) {
    unsigned cnt, k;
    if (lim < 12) return;
    if (!(mpf[0] == 'M' && mpf[1] == 'M')) return; /* spec mandates big endian */
    cnt = be16(mpf + 8);
    if (cnt > 16) return;
    for (k = 0; k < cnt; k++) {
        unsigned q = 10 + k * 12;
        if (q + 12 > lim) return;
        if (be16(mpf + q) == 0xB002) {
            unsigned v = be32(mpf + q + 8);
            unsigned ne = be32(mpf + q + 4) / 16;
            if (ne > 1 && v + 32 <= lim) {
                unsigned size = be32(mpf + v + 16 + 4);
                unsigned off = be32(mpf + v + 16 + 8);
                if (size > 4096) {
                    r->big_len = size;
                    r->big_off = abs_base + off;
                }
            }
            return;
        }
    }
}

static int sof_dim(const unsigned char *b, unsigned lim, unsigned *w, unsigned *h) {
    unsigned i = 2;
    while (i + 9 < lim) {
        unsigned m, ln;
        if (b[i] != 0xFF) return 0;
        m = b[i + 1];
        if (m == 0xFF) {
            i++;
            continue;
        }
        if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;
            continue;
        }
        if (i + 4 > lim) return 0;
        ln = be16(b + i + 2);
        if (ln < 2) return 0;
        if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) ||
            (m >= 0xCD && m <= 0xCF)) {
            *h = be16(b + i + 5);
            *w = be16(b + i + 7);
            return 1;
        }
        if (m == 0xDA) return 0;
        i += 2 + ln;
    }
    return 0;
}

/* 1 when [off, off+len) is a self-contained JPEG stream. */
static int jpeg_ok(int fd, unsigned off, unsigned len, unsigned limit, unsigned *w, unsigned *h) {
    unsigned char b[512]; /* enough to reach the SOF past an EXIF-less header */
    unsigned char tail[2];
    unsigned want;
    if (!len || len < 8 || off + len > limit) return 0;
    want = len < sizeof(b) ? len : (unsigned)sizeof(b);
    if (xseek(fd, off, SEEK_SET) < 0) return 0;
    if (xread(fd, b, (int)want) != (int)want) return 0;
    if (b[0] != 0xFF || b[1] != 0xD8) return 0;
    if (xseek(fd, off + len - 2, SEEK_SET) < 0) return 0;
    if (xread(fd, tail, 2) != 2) return 0;
    if (tail[0] != 0xFF || tail[1] != 0xD9) return 0;
    if (w && h) {
        unsigned bw, bh;
        if (sof_dim(b, want, &bw, &bh)) {
            *w = bw;
            *h = bh;
        }
    }
    return 1;
}

/* The appended image is a wrapper: SOI, then a short Exif holding only the
 * pixel size, then the real SOI + DQT + ... Descend while the next marker after
 * a SOI is another SOI, so callers receive the plain stream not the container. */
static unsigned unwrap_big(int fd, unsigned hint, unsigned limit, unsigned *len_out) {
    unsigned pos = hint > 8 ? hint - 8 : 0;
    int depth;
    unsigned char win[WIN_MAX];
    for (depth = 0; depth < 4; depth++) {
        int got, i;
        unsigned soi = 0, k;
        if (pos >= limit - 4) return 0;
        if (xseek(fd, pos, SEEK_SET) < 0) return 0;
        got = xread(fd, win, sizeof(win));
        if (got < 8) return 0;
        for (i = 0; i + 1 < got; i++) {
            if (win[i] == 0xFF && win[i + 1] == 0xD8) {
                soi = pos + (unsigned)i;
                break;
            }
        }
        if (!soi) return 0;
        k = soi + 2;
        for (;;) {
            unsigned m, ln, rel;
            if (k + 4 > pos + (unsigned)got) {
                /* marker header past the window: too long to unwrap blind,
                 * accept this SOI if it is a sane stream on its own. */
                if (jpeg_ok(fd, soi, limit - soi, limit, 0, 0)) {
                    *len_out = limit - soi;
                    return soi;
                }
                return 0;
            }
            rel = k - pos;
            if (win[rel] != 0xFF) break;
            m = win[rel + 1];
            if (m == 0xD8) {
                pos = k;
                soi = 0;
                break;
            }
            if (m == 0xDA) {
                *len_out = limit - soi;
                return soi;
            }
            ln = be16(win + rel + 2);
            if (ln < 2) break;
            k += 2 + ln;
        }
        if (soi && jpeg_ok(fd, soi, limit - soi, limit, 0, 0)) {
            *len_out = limit - soi;
            return soi;
        }
    }
    return 0;
}

static int build_probe(int fd, probe_t *r, unsigned char *head) {
    int got, i;
    unsigned limit;
    mzero(r, sizeof(*r));
    r->orientation = 1;
    limit = (unsigned)xseek(fd, 0, SEEK_END);
    if (limit < 1024) return 0;
    if (xseek(fd, 0, SEEK_SET) < 0) return 0;
    got = xread(fd, head, HEAD_MAX);
    if (got < 4) return 0;
    if (head[0] != 0xFF || head[1] != 0xD8) return 0;
    sof_dim(head, (unsigned)got, &r->main_w, &r->main_h);
    i = 2;
    for (;;) {
        unsigned m, ln;
        if (i + 4 > got) break;
        if (head[i] != 0xFF) break;
        m = head[i + 1];
        if (m == 0xFF) {
            i++;
            continue;
        }
        if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;
            continue;
        }
        ln = be16(head + i + 2);
        if (ln < 2) break;
        if (m == 0xE1 && i + 10 <= got && meq(head + i + 4, "Exif\0\0", 6))
            parse_tiff(head + i + 10, (unsigned)got - (unsigned)(i + 10), (unsigned)(i + 10), r);
        if (m == 0xE2 && i + 8 <= got && meq(head + i + 4, "MPF\0", 4))
            parse_mpf(head + i + 8, (unsigned)got - (unsigned)(i + 8), (unsigned)(i + 4), r);
        if (m == 0xDA) break;
        i += 2 + ln;
    }
    if (r->thumb_off && r->thumb_len) {
        if (!jpeg_ok(fd, r->thumb_off, r->thumb_len, limit, &r->thumb_w, &r->thumb_h)) {
            r->thumb_off = 0;
            r->thumb_len = 0;
        }
    }
    if (r->big_off && r->big_len) {
        unsigned len = 0;
        unsigned off = unwrap_big(fd, r->big_off, limit, &len);
        if (off) {
            r->big_off = off;
            r->big_len = len;
        } else {
            r->big_off = 0;
            r->big_len = 0;
        }
    }
    return 1;
}

/* 36-byte App1 carrying nothing but Orientation. Index 29 is the value byte.
 * Browsers default to image-orientation: from-image, so they rotate on it. */
static const unsigned char APP1[36] = {
    0xFF, 0xE1, 0x00, 0x22, 'E',  'x',  'i',  'f',  0x00, 0x00, 'M',  'M',
    0x00, 0x2A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x01, 0x12, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Copy [off, off+len) to stdout, dropping the leading two SOI bytes and, when
 * orient > 1, putting our own SOI + APP1 in their place. */
static int emit(int fd, unsigned off, unsigned len, unsigned orient) {
    unsigned char buf[IOBUF];
    unsigned done = 0;
    int first = 1;
    if (xseek(fd, off, SEEK_SET) < 0) return -1;
    while (done < len) {
        unsigned chunk = len - done;
        int got;
        if (chunk > IOBUF) chunk = IOBUF;
        got = xread(fd, buf, chunk);
        if (got <= 0) return -1;
        if (first) {
            /* keep the source SOI, then optionally insert our APP1 so the
             * stream still starts FFD8 and stays a legal JPEG */
            if (got < 2) return -1;
            if (xwrite(1, buf, 2) != 2) return -1;
            if (orient > 1 && orient <= 8) {
                unsigned char app1[36];
                mcopy(app1, APP1, 36);
                app1[29] = (unsigned char)orient;
                if (xwrite(1, app1, 36) != 36) return -1;
            }
            if (got - 2 > 0) {
                if (xwrite(1, buf + 2, got - 2) < 0) return -1;
            }
            first = 0;
        } else {
            if (xwrite(1, buf, got) < 0) return -1;
        }
        done += (unsigned)got;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *verb, *path;
    int fd;
    probe_t p;
    static unsigned char head[HEAD_MAX];

    if (argc < 3) {
        out("usage: nxthumb thumb|big|info <file>\n");
        xexit(1);
    }
    verb = argv[1];
    path = argv[2];
    fd = xopen(path);
    if (fd < 0) {
        xexit(2);
    }
    if (!build_probe(fd, &p, head)) {
        xclose(fd);
        xexit(2);
    }

    if (verb[0] == 'i' && verb[1] == 'n') {
        outdec(p.thumb_len);
        out(" ");
        outdec(p.orientation);
        out(" ");
        outdec(p.big_len);
        out(" ");
        outdec(p.main_w);
        out(" ");
        outdec(p.main_h);
        out(" ");
        outdec(p.thumb_w);
        out(" ");
        outdec(p.thumb_h);
        out("\n");
        xclose(fd);
        xexit(0);
    }

    if (verb[0] == 't') {
        if (!p.thumb_off || !p.thumb_len) {
            xclose(fd);
            xexit(2);
        }
        if (emit(fd, p.thumb_off, p.thumb_len, p.orientation)) {
            xclose(fd);
            xexit(2);
        }
        xclose(fd);
        xexit(0);
    }

    if (verb[0] == 'b') {
        if (!p.big_off || !p.big_len) {
            xclose(fd);
            xexit(2);
        }
        if (emit(fd, p.big_off, p.big_len, p.orientation)) {
            xclose(fd);
            xexit(2);
        }
        xclose(fd);
        xexit(0);
    }

    xclose(fd);
    out("unknown verb\n");
    xexit(1);
}
