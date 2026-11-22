/* NX-KS menus + persistent FilmLab strip. Nuklear / Xlib / Xft, no EFL.
 * The viewfinder stays on its existing camera plane: no frame capture or blit.
 * Rendering is event driven; the only timer is backend/debounce housekeeping.
 */
#define _POSIX_C_SOURCE 200809L
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#include "vendor/nuklear.h"
#define NK_XLIB_USE_XFT
#define NK_XLIB_IMPLEMENTATION
#include "vendor/nuklear_xlib.h"
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "nxui_model.h"
/* evdev（S1 半按快门）：结构手动定义，不依赖内核头 */
struct input_event { long tv_sec, tv_usec; unsigned short type, code; int value; };
#define EV_KEY 1
#include "nxui_services.h"

/* 菜单网格：3 列 × 4 行 = 12 项/页（8a12fe7 从 2×3 改的）。
 * ★ services_visible() 里原来还写死 6 项/页，于是第 7 项之后的服务开关
 *   永远不刷新状态（面板显示的第一页数也不对）。改成同一个常量。 */
#define MENU_COLS 3
#define MENU_ROWS 4
#define MENU_PAGE (MENU_COLS * MENU_ROWS)
static struct nx_item items[NXUI_MAX_ITEMS];
static const char *root = "/opt/usr/nx-ks";
static int item_count, menu, page, live, dirty = 1, quit;
/* ── 两层滤镜：LUT(A18 槽) 与 FilmLab(prefman PW) ──────────────────
 * 2026-11-20 方案 2b：EV+WiFi 是唯一入口，菜单页里的「LUT」「FilmLab」
 * 不再 fork+exec 第二个 nxui（要 ~1 秒，且 pidof 会命中正在退出的自己 →
 * 「只杀不起」），改为同一进程内换层。
 * 两层各有自己的列表、引擎、选中项与应用队列：换层只换视图，
 * 不动任何一层的相机状态（LUT 走 A18，PW 走 prefman，互不覆盖）。
 * ★ 面板刻意不显示「本层是否已在机器上生效」：那是引擎的知识，面板只
 *   知道自己这次应用过什么。归因靠「切到该层按还原」这条可控隔离路径，
 *   不靠假读数。 */
#define L_LUT 0
#define L_PW  1
#define NLAYER 2
struct nx_layer {
    struct nx_recipe rec[NXUI_MAX_RECIPES];
    int n, sel;                 /* 字段名故意短：count / selected / engine 是
                                 * 下面的宏，成员名跟宏同名会被预处理器替换 */
    const char *path, *eng;
    struct nx_queue q;
};
static struct nx_layer layers[NLAYER];
static struct nx_layer *cur = &layers[L_PW];
/* ── LUT 层的两个输入域：std / vlog（2026-11-20）────────────────────
 * vlog 表和普通表写的是同一个 A18 槽，区别只在「这张表是按哪种输入域做的」：
 * 普通表吃 8bit sRGB 素材，vlog 表吃对数域素材。所以它们不是第三层，是同一
 * 层的两张清单 —— 开第三个页签会把 720px 顶栏挤薄，也让「换层」和「换域」
 * 两种语义混在同一排按钮里。
 * 入口：**已经是 LUT 层时再点一次 LUT 页签 = 换域**（页签文字带 VLOG 标记）。
 * 两个域各自记住停在哪一项；列表按需重读，不额外占一份 256 项数组
 * （相机上 BSS 是要省着用的）。 */
static const char *lut_path_std, *lut_path_vlog;
static int lut_scope;                  /* 0 = std（普通表）1 = vlog（对数域表） */
static char scope_key[2][96];          /* 每个域上次停的 look 名 */
#define recipes  (cur->rec)
#define count    (cur->n)
#define selected (cur->sel)
#define engine   (cur->eng)
#define queue    (cur->q)
#define lut_mode (cur == &layers[L_LUT])
static char return_menu[NXUI_PATH];   /* 进滤镜前停在哪个菜单页 */
/* Frame pacing is a device-tunable, not a constant: 100 ms protects the camera
 * from a redraw storm, but it is also the worst-case delay between a tap and
 * the next visible frame. NXUI_FRAME_MS lets the operator A/B it on the body
 * without a rebuild; the default keeps the measured 10 Hz behaviour. */
static int frame_ms = 100;
static int bench_frames;      /* --bench-frames: render N frames back to back, then exit */
static long long draw_total_ms; static int draw_calls;
/* 同一条滤镜条驱动两个引擎：LUT 层发 lut.sh，PW 层发 filmlab.sh（lut_mode
 * 现在是「当前层是不是 LUT」的宏，见上面的 layers 块）。
 * 离开面板不还原任何东西：LUT 的每次写入都只落在 RAM（断电即恢复镜像里的
 * 原表），所以「退出即撤销」保护不了什么，反而在最自然的退出路径
 * （「保存并关闭」、OK、半按快门）上把用户刚选好的效果撤掉。
 * 还原是显式按钮。 */
static int close_after_child;   /* 触屏滑动手势已于 11-02 删除，touch_x/active 随之作废 */
/* ── 库管理 / 混合 / 输入（2026-11-01）───────────────────────────────
 * 三个 UI 状态共用一个滤镜条，只换下面那排按钮 —— 720×208 里没有第二个
 * 面板的位置，而"换掉按钮行"比新开一层窗口更不容易挡住取景器：
 *   BROWSE 浏览（原有行为）
 *   EDIT   长按进入：删除 / 左移 / 右移 / 完成（删除要两次点确认）
 *   MIX    双表交叉混合：设为A / 设为B / 应用 / 退出，滚条变成 0-100 强度
 * 混合本身不在面板里算：面板只发 `lut.sh mix A B pct`，表由 lutmix.arm 生成。
 * 这样面板永远不持有像素数据，也不需要为 19,712 字节的缓冲区估内存。 */
#define UI_BROWSE 0
#define UI_EDIT   1
#define UI_MIX    2
static int ui_mode = UI_BROWSE;
static int confirm_del;              /* 删除按钮的二次确认窗口 */
static long long confirm_del_at;
/* 双强度：A、B 各自 0..100。落到表上按「归一加权」算：
 *   r = rb/(ra+rb)          A/B 的比例（0=全 A，100=全 B）
 *   s = min(ra+rb, 100)     总强度（离原厂多远）
 *   T = 原厂 + s·(mix(A,B,r) − 原厂)
 * 这样任何组合都不会外推出 0..255，也与 A/B 顺序无关
 * （三种语义的实测对比见 docs/current/DUAL_STRENGTH_2026-11-02.md）。 */
static int mix_a = -1, mix_b = -1;
static int mix_ra = 100, mix_rb = 100;   /* A、B 各自的强度 */
static int mix_focus;                    /* MIX 态正在调哪一个：0=A 1=B */
static char mix_key_a[96], mix_key_b[96];   /* 存 key 不存序号：库增删后序号会指错 */
/* 库变更后要重建列表：1 = 重跑 `lut.sh list`（LUT 面板），2 = 重读配方表 */
static int refresh_pending, refresh_after;
#define recipes_path_global (cur->path)
/* 长按：按下计时，手指移动超过阈值就当作滑动，不再触发 */
static int press_active, press_x, press_y;
static long long press_at;
static const char *inputlog_path; static FILE *inputlog;
/* A half-press usually lands inside the 150 ms debounce window that follows the
 * last swipe: the highlighted look is the one the operator wants, but the queue
 * has not dispatched it yet. Dropping that request on close meant the slot kept
 * the previous look while the panel showed the new one. So a close with an
 * unsettled LUT selection drains that one request first. This flag marks the
 * drained run: once the panel has been unmapped there is nowhere to show an
 * error, so a failure must still exit instead of leaving an invisible loop. */
static int drain_on_close;
/* 记住上次打开时停在哪一项。存的是 key（配方 slug / look 名）而不是序号：
 * 库会增删条目，序号一变就指错项，而名字是稳定的。两个模式各一个文件，
 * 谁也不用解析对方的格式。写失败只是下次从头开始，不影响任何功能。 */
static const char *state_dir;   /* NXUI_STATE_DIR；未设置就不记位置（测试默认） */
static char last_choice[96];
static char enum_cache[16]; static long long enum_cache_ms;
static long long close_flag_check;
static int ev_fd = -1;
static int busy_kind, child_fd = -1, output_len, child_timed_out;
static pid_t child;
static long long child_started, lifetime;
static long long service_due;
static struct nx_services services;
static const char *net_dir = "/proc/net";
/* busy_* 记的是「这条子进程是为哪一层的哪一项起的」。子进程跑完后用户可能
 * 已经换到另一层，此时 recipes[busy_recipe] 会指到错的那张表，所以层号必须
 * 跟着一起记下来。 */
static int backend_phase, busy_recipe = -1, busy_layer = L_PW, pipe_eof;
static char output[4096], status[384] = "选择配方以预览 · 保存后保留设置";
/* 面板内横幅：脚本往 /tmp/nxui-banner 写一行 "<文字>\t<秒>"，面板读走删掉并
 * 自己在最上层画出来。用它替代 popup_timeout —— 后者是独立窗口，会被面板
 * 盖住，等于没有反馈。 */
static char banner[256];
static long long banner_until, banner_check;
/* 多行对话框：/tmp/nxui-dialog 整个文件当正文（一行一条，最多 8 行），
 * 不再自动消失 —— 原版 popup 一条条弹、每条 3 秒自己关，看四行版本信息要等
 * 12 秒还记不住。这里一次显示完，右下角一个「关闭」，点它或按任意退出键才收。 */
static char dialog[1024];
static int dialog_on;
static struct nk_rect dialog_close;   /* 由绘制写入，点击时判命中 */
static char menu_path[NXUI_PATH];
static char pending_menu[NXUI_PATH];
static volatile sig_atomic_t interrupted;
static Display *display;
static Window window;
static int screen, width, height;
static struct nk_context *ctx;
/* The camera runs without a window manager: an XSetInputFocus taken by this
 * panel is never taken back by anyone else. Camera menu navigation (jog wheel
 * and arrow keys) arrives as X KeyPress, which is delivered only to the focus
 * window, so the panel has to record the previous focus and hand it back when
 * it closes. Otherwise the native menu looks like its keys stopped working. */
static Window prev_focus;
static int prev_revert = RevertToPointerRoot;
static int focus_saved, focus_restored, panel_unmapped;

static int ignore_xerror(Display *dpy, XErrorEvent *ev) { (void)dpy; (void)ev; return 0; }

static void save_focus(void) {
    XErrorHandler old;
    int revert = RevertToPointerRoot;
    if (!display) return;
    old = XSetErrorHandler(ignore_xerror);
    XGetInputFocus(display, &prev_focus, &revert);
    XSetErrorHandler(old);
    prev_revert = revert;
    focus_saved = 1; focus_restored = 0;
}

static void restore_focus(void) {
    XErrorHandler old;
    if (!display || !focus_saved || focus_restored) return;
    focus_restored = 1;
    old = XSetErrorHandler(ignore_xerror);
    /* The previous window may be gone (an entry script may have killed the old
     * menu). Fall back to PointerRoot so key events still follow the pointer
     * instead of being dropped. */
    XSetInputFocus(display, prev_focus == None ? PointerRoot : prev_focus,
        prev_revert, CurrentTime);
    XSync(display, False);
    XSetErrorHandler(old);
}

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void on_signal(int sig) { (void)sig; interrupted = 1; }
static void on_child(int sig) { (void)sig; } /* interrupt select; waitpid stays in the main loop */
static void message(const char *s) { snprintf(status, sizeof status, "%s", s); dirty = 1; }
static const char *base_name(const char *p) {
    const char *last = strrchr(p, '/'); return last ? last + 1 : p;
}
static int services_visible(void) {
    int i;
    if (!menu) return 0;
    for (i = page*MENU_PAGE; i < item_count && i < page*MENU_PAGE+MENU_PAGE; i++)
        if (!strcmp(base_name(items[i].command), "telnet_toggle.sh")) return 1;
    return 0;
}
static void refresh_services(int force) {
    struct nx_services next; char label[192];
    if (!force && (!services_visible() || now_ms() < service_due)) return;
    nx_services_read(&next, net_dir);
    service_due = now_ms() + 1000;
    if (!nx_services_equal(&services, &next)) {
        services = next; dirty = 1;
        nx_services_label(&services, label, sizeof label);
        fprintf(stderr, "SERVICES known=%d telnet=%d ftp=%d label=%s\n", services.known, services.telnet, services.ftp, label);
    }
}
static void preview_status(void) {
    /* 子进程属于哪一层，就用哪一层的列表与队列 —— 完成后用户可能已经换层，
     * 拿当前层去解释「正在保存 X」会指到另一张表上。 */
    struct nx_layer *bl = &layers[busy_layer];
    if (child && busy_kind == 1 && bl->q.desired >= 0 && bl->q.desired != bl->q.running)
        snprintf(status, sizeof status, "已选择 %s · 等待当前预览完成", bl->rec[bl->q.desired].label);
    else if (child && busy_kind == 1 && busy_recipe >= 0)
        snprintf(status, sizeof status, "%s：%s", backend_phase == 2 ? "正在确认" : (bl->q.committing ? "正在保存" : "正在发送"), bl->rec[busy_recipe].label);
    else snprintf(status, sizeof status, "%s：%s", live ? "准备预览" : "演示选择", recipes[selected].label);
    dirty = 1;
}
static int checked(const char *name) {
    char path[NXUI_PATH];
    snprintf(path, sizeof path, "%s/auto/%s", root, base_name(name));
    return access(path, F_OK) == 0;
}
static void resize_panel(void) {
    /* 208 not 192：新增 14px 横向滚动条 + 5px 行距后，192 会把按钮行挤出
     * 面板底部（Nuklear 直接丢弃后续行，表现为按钮整行不渲染）。 */
    height = menu ? 292 : 208;
    if (height > DisplayHeight(display, screen)) height = DisplayHeight(display, screen);
    XMoveResizeWindow(display, window, (DisplayWidth(display, screen)-width)/2,
        DisplayHeight(display, screen)-height, (unsigned)width, (unsigned)height);
    /* Resize the official backend's pixmap as well as the native window. */
    nk_xsurf_resize(xlib.surf, (unsigned)width, (unsigned)height);
    dirty = 1;
}
/* 换层（方案 2b）：只换视图，不改任何一层的相机状态。旧层里还没落地的
 * 选择不丢 —— 切换那一刻把它标成立即到期（due=0），主循环按层派发。 */
static void set_layer(int want) {
    if (want < 0 || want >= NLAYER || want == (int)(cur - layers)) return;
    if (cur->q.desired >= 0) cur->q.due = 0;
    cur = &layers[want];
    ui_mode = UI_BROWSE; confirm_del = 0; press_active = 0;
    fprintf(stderr, "LAYER %s entries=%d sel=%d\n", lut_mode ? "lut" : "pw", count, selected);
    message(lut_mode ? "LUT 层 · 选中即写 A18 槽" : "FilmLab 层 · 选中即推 PW");
    dirty = 1;
}
/* 换域（std ⇄ vlog），不换层：只重读一份清单，队列/引擎/A18 槽都不动。
 * 只在「当前已经是 LUT 层」时由页签的第二次点击触发，见 lut_scope 注释。 */
static int scope_sel(void) {
    int i, n = layers[L_LUT].n;
    if (!scope_key[lut_scope][0]) return 0;
    for (i = 0; i < n; i++)
        if (!strcmp(layers[L_LUT].rec[i].key, scope_key[lut_scope])) return i;
    return 0;
}
static void toggle_lut_scope(void) {
    struct nx_layer *L = &layers[L_LUT];
    const char *path;
    int next = !lut_scope, n;
    if (child) { message("正在应用 · 完成后再切范围"); return; }
    if (next && (!lut_path_vlog || !*lut_path_vlog)) { message("没有配 vlog 表清单"); return; }
    if (L->sel >= 0 && L->sel < L->n)
        snprintf(scope_key[lut_scope], sizeof scope_key[0], "%s", L->rec[L->sel].key);
    lut_scope = next;
    path = lut_scope ? lut_path_vlog : lut_path_std;
    L->path = path;
    /* 队列按序号记账。换域后同一个序号指到另一张表的另一项 —— 不在这里作废，
     * 一个还没派发的选择就会拿 vlog 的序号去 apply 普通表的那一项。
     * applied=-1 表示「当前选中尚未落地」，desired=-1 表示没有待发的写入。 */
    L->q.desired = L->q.applied = -1;
    n = nx_load_recipes(path, L->rec, NXUI_MAX_RECIPES);
    L->n = n > 0 ? n : 0;
    L->sel = scope_sel();
    /* 混合的两个端点也是序号，跨域混一张 std 一张 vlog 没有意义 */
    mix_a = mix_b = -1; mix_key_a[0] = mix_key_b[0] = 0;
    if (ui_mode == UI_MIX) ui_mode = UI_BROWSE;
    fprintf(stderr, "SCOPE %s entries=%d sel=%d path=%s\n",
            lut_scope ? "vlog" : "std", L->n, L->sel, path);
    if (L->n <= 0)
        message(lut_scope ? "V-Log 表目录为空 · 先导入 vlog 表" : "LUT 库为空");
    else
        message(lut_scope ? "V-Log 表 · 按对数域输入做的，配 V-Log 素材"
                          : "标准表 · 再点 LUT 页签可切到 V-Log 表");
    dirty = 1;
}
static int load_menu(const char *source) {
    static struct nx_item pending[NXUI_MAX_ITEMS];
    char next[NXUI_PATH]; int n;
    /* 失败必须留一行可 grep 的日志：菜单读不出来时面板只会显示一句中文，
     * 而测试与现场排障都只看 stderr —— 之前「子菜单生成成功但没切过去」
     * 就是这么变成静默失败的。 */
    if (!nx_menu_path(root, source, next, sizeof next)) {
        fprintf(stderr, "MENU FAIL path source=%s root=%s\n", source, root);
        message("菜单读取失败 · 详情见日志"); return 0;
    }
    if ((n = nx_load_menu(next, pending, NXUI_MAX_ITEMS)) <= 0) {
        fprintf(stderr, "MENU FAIL parse file=%s items=%d\n", next, n);
        message("菜单读取失败 · 详情见日志"); return 0;
    }
    memcpy(items, pending, (size_t)n * sizeof *items);
    snprintf(menu_path, sizeof menu_path, "%s", next);
    item_count = n; page = 0; menu = 1;
    fprintf(stderr, "MENU file=%s items=%d\n", menu_path, item_count);
    resize_panel(); return 1;
}
static void choose(int index) {
    if (count <= 0 || queue.close_requested) return;
    /* 任意步长（滑动手势一次可跨好几项）都要落回 0..count-1：
     * C 的 % 对负数返回负值，直接 (index+count)%count 在 index<-count 时越界。 */
    selected = ((index % count) + count) % count;
    /* An explicit tap also reapplies after a camera/legacy-menu effect change. */
    queue.applied = -1;
    nx_queue_select(&queue, selected, now_ms());
    preview_status();
}
static void read_output(void) {
    char buf[512]; ssize_t n;
    if (child_fd < 0) return;
    while ((n = read(child_fd, buf, sizeof buf)) > 0) {
        int keep = (int)n;
        if (keep > (int)sizeof(output)-1-output_len) keep = (int)sizeof(output)-1-output_len;
        if (keep > 0) { memcpy(output + output_len, buf, (size_t)keep); output_len += keep; }
    }
    if (n == 0) pipe_eof = 1;
    output[output_len] = 0;
    if (busy_kind == 1 && backend_phase != 2 && strstr(output, "NXUI_STAGE restore")) {
        backend_phase = 2; preview_status();
    }
}
static int start_child(int kind, int index, const char *command) {
    int pipefd[2]; pid_t pid;
    if (child) return 0;
    if (!live) {
        if (kind == 1) {
            nx_queue_done(&queue, 1);
            message("演示模式 · 已选择，未写入相机");
            if (queue.committing) quit = 1;
        } else message("演示模式 · 未执行菜单命令");
        return 1;
    }
    if (pipe(pipefd) < 0) return 0;
    pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return 0; }
    if (pid == 0) {
        char envpath[1024]; const char *old = getenv("PATH");
        setpgid(0, 0);
        close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]); close(ConnectionNumber(display));
        if (chdir(root) != 0) _exit(126);
        snprintf(envpath, sizeof envpath, "%s:%s", root, old ? old : "/usr/bin:/bin");
        setenv("PATH", envpath, 1);
        if (kind == 1 && enum_cache[0]) setenv("FILMLAB_ENUM_CACHE", enum_cache, 1);
        if (kind == 1) {
            /* In LUT mode a dry run must not touch the registry or the slot:
             * "apply" is a real write, so it is only used when live. */
            const char *action = lut_mode ? (live ? "apply" : "status")
                                : (queue.committing ? "commit" : "preview");
            execl("/bin/sh", "sh", engine, action, recipes[index].key, (char *)0);
        }
        /* 引擎的动词走 sh 解释，不走 `bash -c "<path> verb"`：后者要求引擎带
         * 可执行位，而 apply/preview 一直是不需要的 —— 同一条链上出现两种
         * 前提只会让部署状态更难判断。 */
        else if (kind == 7) execl("/bin/sh", "sh", engine, "neutral", (char *)0);
        else if (kind == 8) execl("/bin/sh", "sh", engine, "off", (char *)0);
        else execl("/bin/bash", "bash", "-c", command, (char *)0);
        _exit(127);
    }
    setpgid(pid, pid);
    close(pipefd[1]); fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    child_fd = pipefd[0]; child = pid; busy_kind = kind;
    busy_recipe = index; busy_layer = (int)(cur - layers); backend_phase = 0; pipe_eof = 0;
    child_started = now_ms(); child_timed_out = 0; output_len = 0;
    output[0] = 0;
    if (kind == 1) preview_status(); else message("正在执行…");
    return 1;
}
/* ── 两层的派发与收尾（方案 2b，2026-11-20）─────────────────────────
 * 同一时刻只跑一个子进程：两个引擎都在写相机状态，并发只会让归因更难。
 * 按层扫一遍，谁到期先发谁。换层时旧层被标成 due=0，于是排在最前面 ——
 * 在 LUT 里选好就切去 PW，那次写入不会因为换了视图而丢掉。 */
static int lut_unsettled(void) {
    struct nx_queue *q = &layers[L_LUT].q;
    return q->desired >= 0 && q->desired != q->applied;
}
static void dispatch_pending(void) {
    int L, saved = (int)(cur - layers), idx;
    for (L = 0; L < NLAYER && !child; L++) {
        cur = &layers[L];
        while (!child && (idx = nx_queue_take(&queue, now_ms())) >= 0) {
            if (lut_mode && queue.close_requested) drain_on_close = 1;
            if (!start_child(1, idx, NULL)) {
                drain_on_close = 0;
                nx_queue_done(&queue, 0);
                message("后端启动失败");
            }
        }
    }
    cur = &layers[saved];
}
static int rebuild_lut_list(const char *text);
static int reload_recipes_file(void);
static int run_engine(const char *verb, const char *a1, const char *a2,
                      const char *a3, int refresh);
static int run_engine4(const char *verb, const char *a1, const char *a2,
                       const char *a3, const char *a4, int refresh);
static void poll_child(void) {
    int code, success, was_commit; pid_t result;
    if (!child) return;
    read_output(); result = waitpid(child, &code, WNOHANG);
    if (result == 0) {
        long long age = now_ms() - child_started;
        /* LUT apply/off include synchronous camera events. Give a slow event
         * time to finish and allow its restoration handler to run on TERM. */
        long long limit = busy_layer == L_LUT && (busy_kind == 1 || busy_kind == 8) ? 30000 : 12000;
        long long grace = busy_layer == L_LUT && (busy_kind == 1 || busy_kind == 8) ? 5000 : 1000;
        if (age > limit && !child_timed_out) {
            child_timed_out = 1; kill(-child, SIGTERM);
            message("后端超时 · 正在停止，请检查相机状态");
        }
        if (age > limit + grace) kill(-child, SIGKILL);
        return;
    }
    if (result < 0 && errno == EINTR) return;
    read_output(); close(child_fd); child_fd = -1; child = 0;
    success = result > 0 && !child_timed_out && WIFEXITED(code) && WEXITSTATUS(code) == 0 &&
        !strstr(output, "VERIFY-FAIL") && !strstr(output, "REJECT");
    fprintf(stderr, "BACKEND kind=%d ok=%d elapsed_ms=%lld output=%s\n", busy_kind, success, now_ms()-child_started, output);
    if (busy_kind == 1) {
        /* 这条子进程属于 busy_layer，不是"当前层"：跑的过程中用户可能已经
         * 换了页签。队列与列表都按它自己的层记账，否则另一层的 running
         * 永远不清零，那边的写入从此再也派发不出去。 */
        struct nx_layer *bl = &layers[busy_layer];
        was_commit = bl->q.committing;
        nx_queue_done(&bl->q, success);
        if (bl->q.desired >= 0 && bl->q.desired != busy_recipe) preview_status();
        else if (success) {
            snprintf(status, sizeof status, "已发送：%s", bl->rec[busy_recipe].label); dirty = 1;
        } else message("应用失败 · 可重新选择以重试");
        if (was_commit && success) quit = 1;
        /* A drained-on-close run has no panel left to report to: exit either
         * way. The slot write itself is verified inside lut.sh, and a failed
         * one leaves the previous look live -- exactly what a plain close
         * would have done, just with an accurate log line. */
        if (!success && drain_on_close) { quit = 1; bl->q.close_requested = 0; }
        else if (!success) { bl->q.close_requested = 0; bl->q.commit_requested = 0; }
        drain_on_close = 0;
    } else if (busy_kind == 3) {
        if (success && load_menu(pending_menu)) message("请选择菜单项");
        else message("子菜单生成失败 · 可重新进入");
        pending_menu[0] = 0;
    } else if (busy_kind == 4) {
        refresh_services(1);
        if (!services.known) message("服务状态读取失败 · 请重试");
        else {
            snprintf(status, sizeof status, "%sTelnet 已%s · FTP 已%s", success ? "" : "未完全成功 · ",
                services.telnet ? "开启" : "关闭", services.ftp ? "开启" : "关闭");
            dirty = 1;
        }
    } else if (busy_kind == 6) {
        /* 解析 "UserData is PW_CUSTOM1 (0x140009)" → 缓存 hex，供 preview 免读 */
        char *p = strstr(output, "(0x");
        if (p) {
            char *q = p + 1, *e = strchr(q, ')');
            if (e && e - q < (int)sizeof enum_cache) {
                memcpy(enum_cache, q, (size_t)(e - q));
                enum_cache[e - q] = 0;
                enum_cache_ms = now_ms();
                dirty = 1;
            }
        }
    } else if (busy_kind == 7) {
        message(success ? "已还原中性 · 切档或重启回原厂" : "还原失败 · 详情见日志");
    } else if (busy_kind == 8) {
        message(success ? "已还原原厂表" : "还原失败 · 详情见日志");
    } else if (busy_kind == 5) {
        /* 库变更后重跑 `lut.sh list`：输出就是新的顺序。列表只属于 LUT 层，
         * 重建成败与当前停在哪一层无关 —— 临时切过去再切回来。 */
        int saved = (int)(cur - layers);
        cur = &layers[L_LUT];
        if (!success) message("库列表刷新失败 · 见 /tmp/nxui.log");
        else if (rebuild_lut_list(output)) message("库已刷新");
        else message("库读空了 · 保留原列表");
        cur = &layers[saved];
    } else {
        if (!success) message(refresh_after ? "操作失败 · 详情见日志" : "命令失败 · 详情见日志");
        else if (refresh_after) { refresh_pending = refresh_after; message("已完成 · 正在刷新库"); }
        else if (close_after_child) { /* fall through to the close chain */ }
        else {
            /* 菜单里的脚本经常用一行 `ERR ...` 说清楚为什么失败（例如
             * vlog.sh 那句 "ERR View4 is not factory; disable other LUT mods"）。
             * 它们同时还弹 popup，但 popup 会被这块面板盖住 —— 用户看到的就是
             * "点了没反应"。所以把第一行 ERR/FAIL 直接搬到面板的状态行上。 */
            const char *e = output, *hit = NULL;
            size_t hitlen = 0;
            while (*e) {
                const char *nl = strchr(e, '\n');
                size_t len = nl ? (size_t)(nl - e) : strlen(e);
                if ((len > 3 && (!strncmp(e, "ERR", 3) || !strncmp(e, "FAIL", 4))) ||
                    (len > 6 && !strncmp(e, "REJECT", 6))) {
                    hit = e; hitlen = len;
                }
                if (!nl) break;
                e = nl + 1;
            }
            if (hit) {
                char tmp[192];
                if (hitlen > sizeof tmp - 1) hitlen = sizeof tmp - 1;
                memcpy(tmp, hit, hitlen); tmp[hitlen] = 0;
                message(tmp);
            } else message("命令已完成");
        }
        refresh_after = 0;
        if (close_after_child) {
            close_after_child = 0;
            if (success) { quit = 1; queue.close_requested = 1; }
        }
    }
    dirty = 1;
}
/* ═══════════════════════ 键位映射 ═══════════════════════════════════
 * 相机在无窗口管理器下把硬件键翻译成 X KeyPress 发给焦点窗口，但机身
 * 转盘/方向键到底映射成哪个 keysym，只能在机身上量（--inputlog）。
 * 所以这里做一张可覆盖的表：默认够用，量到新键就写进 keys.conf，
 * 不必为了一个按键改二进制。 */
enum { ACT_NONE = 0, ACT_PREV, ACT_NEXT, ACT_OK, ACT_CLOSE, ACT_EDIT, ACT_MIX,
       ACT_DEL, ACT_MOVE_L, ACT_MOVE_R, ACT_INC, ACT_DEC, ACT_LEFT, ACT_RIGHT,
       ACT_SWITCH };
static const char *const act_names[] = { "none", "prev", "next", "ok", "close",
    "edit", "mix", "del", "movel", "mover", "inc", "dec", "left", "right",
    "switch" };
/* 24 条不够了：桌面 10 + 机身实测 14，正好卡满，再加一个就静默丢键。 */
#define KEYMAP_MAX 40
/* code 非 0 表示按「X keycode」匹配，用来接那些**没有 keysym** 的键：
 * 相机 X 键位表里 keycode 222/224 后面是空的（NoSymbol），XLookupKeysym
 * 返回 0，用符号表根本绑不上 —— 而 WiFi 键是 XF86Mail(223)、AEL 紧挨着它。 */
static struct { KeySym sym; unsigned code; int act; } keymap[KEYMAP_MAX];
static int keymap_n;

static int action_from_name(const char *s) {
    int i;
    for (i = 1; i < (int)(sizeof act_names / sizeof *act_names); i++)
        if (!strcmp(s, act_names[i])) return i;
    return ACT_NONE;
}
/* keys.conf 里可以写 keysym 名字，也可以写 0x1008FF73 这样的数字。
 * 数字形式是给「名字解析不出来」留的后路：相机上这份 libX11 只有 79 个
 * XF86* 名字（strings 实测），名字表比桌面小，但不是每个键都在里面。 */
static KeySym parse_keysym(const char *s) {
    const char *p;
    unsigned long v = 0;
    if (!s || !*s) return NoSymbol;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (p = s + 2; *p; p++) {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else return NoSymbol;
            v = v * 16u + (unsigned long)d;
        }
        return (KeySym)v;
    }
    return XStringToKeysym(s);
}
/* 注册一个 keysym→动作。**同名 keysym 是覆盖，不是追加**：默认表先装载，
 * keys.conf 后装载，若只追加则「改动一个已有键的含义」永远无效（keymap_action
 * 取第一个匹配，默认条目排在前面）。act == ACT_NONE 表示解绑 —— keys.conf
 * 里写 `KP_Enter none` 就能把一个键摘掉，采数时正需要这个。 */
static void keymap_add(const char *name, int act) {
    KeySym k = NoSymbol;
    unsigned code = 0;
    int i;
    if (!name || !*name || keymap_n >= KEYMAP_MAX) return;
    /* `#222` = 按 X keycode 绑定。相机上有些键没有 keysym（NoSymbol），
     * 只能这样接；keys.conf 里同样可以写。 */
    if (name[0] == '#') {
        const char *p = name + 1;
        if (!*p) return;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') { fprintf(stderr, "KEYMAP bad keycode %s\n", name); return; }
            code = code * 10u + (unsigned)(*p - '0');
        }
        if (!code) { fprintf(stderr, "KEYMAP bad keycode %s\n", name); return; }
    } else {
        k = parse_keysym(name);
        if (k == NoSymbol) { fprintf(stderr, "KEYMAP unknown keysym %s\n", name); return; }
    }
    for (i = 0; i < keymap_n; i++) {
        if (keymap[i].sym != k || keymap[i].code != code) continue;
        if (act == ACT_NONE) {
            memmove(&keymap[i], &keymap[i + 1],
                    (size_t)(keymap_n - i - 1) * sizeof keymap[0]);
            keymap_n--;
        } else keymap[i].act = act;
        return;
    }
    if (act == ACT_NONE) return;          /* 本来就没注册，解绑是空操作 */
    keymap[keymap_n].sym = k; keymap[keymap_n].code = code;
    keymap[keymap_n].act = act; keymap_n++;
}
static void keymap_defaults(void) {
    /* 桌面 / Xvfb 的标准键：只服务于本机回归（verify_display、库管理 UI 测试
     * 都靠它们发 F1/F2/方向键）。机身上不存在这些键，别把它们当交互设计。 */
    /* 左右与上下分开：上下永远是翻页，左右在混合态变成「选 A / 选 B」。
     * 分成两个动作是为了让 MIX 态能区分「左右」和「上下」—— 都绑 prev/next
     * 的话做不到这一点（键位表按动作分发，不按物理位置）。 */
    keymap_add("Left", ACT_LEFT);   keymap_add("Up", ACT_PREV);
    keymap_add("Right", ACT_RIGHT); keymap_add("Down", ACT_NEXT);
    keymap_add("Return", ACT_OK);  keymap_add("KP_Enter", ACT_OK);
    keymap_add("space", ACT_OK);
    keymap_add("Escape", ACT_CLOSE);
    keymap_add("F1", ACT_EDIT);    /* 仅桌面；机身靠长按进编辑态 */
    keymap_add("F2", ACT_MIX);     /* 仅桌面；机身上另有顶栏「混合」按钮 */

    /* ★ 机身键位实测（2026-11-02，LUT 面板 + --inputlog，用户逐键按）：
     *     上 KP_Up(80)      下 KP_Down(88)
     *     左 KP_Left(83)    右 KP_Right(85)
     *     OK KP_Enter(104)  MENU Menu(135)   BACK KP_Delete(91)
     *     播放 XF86Tools(191)   Fn XF86HomePage(180)
     *     EV 拨盘 左 XF86AudioPrev(173) / 右 XF86AudioNext(171)
     *     右上拨盘 左 XF86ScrollDown(186) / 右 XF86ScrollUp(185)
     *     模式拨盘 F8(74) / F9(75)
     * ⇒ 10-17 那条「方向键 / 转盘在 X11 无事件」是**错的**：那次用
     *   override-redirect 探针窗口，没抢到 XSetInputFocus，键被别的窗口吃了。
     *   面板自己抢焦点，16 个键一个不缺，**转盘也是每格一个 KeyPress**，
     *   不是滚轮事件 —— 所以能当连续调节器用。
     * 模式拨盘的 F8/F9 刻意不绑：相机切模式时自己要用，误触代价高。
     * 半按快门 = X11 的 Super_L = evdev code 125(KEY_LEFTMETA)，由 evdev 那条
     * 路径处理（半按关闭面板），不占键位表。 */
    keymap_add("KP_Up", ACT_PREV);    keymap_add("KP_Left", ACT_LEFT);
    keymap_add("KP_Down", ACT_NEXT);  keymap_add("KP_Right", ACT_RIGHT);
    keymap_add("XF86AudioPrev", ACT_PREV);   /* EV 拨盘 ← */
    keymap_add("XF86AudioNext", ACT_NEXT);   /* EV 拨盘 → */
    keymap_add("Menu", ACT_CLOSE);           /* MENU 键当退出 */
    keymap_add("KP_Delete", ACT_CLOSE);      /* BACK 键当退出 */
    keymap_add("XF86Tools", ACT_EDIT);       /* 播放键 = 进编辑态，省掉长按 */
    keymap_add("XF86HomePage", ACT_MIX);     /* Fn = 进混合态 */
    keymap_add("XF86ScrollDown", ACT_DEC);   /* 右上拨盘 ← */
    keymap_add("XF86ScrollUp", ACT_INC);     /* 右上拨盘 → */
    keymap_add("XF86Reload", ACT_PREV);      /* 10-17 采到过，留兼容 */
    keymap_add("XF86Favorites", ACT_NEXT);

    /* ★ 2026-11-20 方案 2b：滤镜页里「单击 WiFi 或 AEL」= 换层。
     * 依据是相机自己的 X 键位表（`xmodmap -pke` 实测）：
     *   keycode 223 = XF86Mail     ← WiFi 键（evdev 215 + 8，EV_MOBILE.sh 同源）
     *   keycode 222/224 = 无 keysym（表里是空的）
     * AEL 与 MOBILE 在 keyscan 的名字表里相邻，所以它只可能是 214 或 216
     * （+8 → 222 / 224）。两个都绑上，实测哪一个都不吃亏；这两个键号在
     * 面板里本来就没有别的含义。keys.conf 里可以改成任何 `#<code>`。 */
    keymap_add("XF86Mail", ACT_SWITCH);   /* WiFi（有 keysym，按名字绑） */
    keymap_add("#223", ACT_SWITCH);       /* 同一个键，按 keycode 兜底 */
    keymap_add("#222", ACT_SWITCH);       /* AEL 候选 1 */
    keymap_add("#224", ACT_SWITCH);       /* AEL 候选 2 */
}
static void keymap_load(void) {
    char path[256], line[160], name[64], act[32];
    FILE *f;
    if (!state_dir || !*state_dir) return;
    snprintf(path, sizeof path, "%s/keys.conf", state_dir);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (sscanf(line, "%63s %31s", name, act) != 2) continue;
        keymap_add(name, action_from_name(act));
    }
    fclose(f);
    fprintf(stderr, "KEYMAP conf=%s entries=%d\n", path, keymap_n);
}
/* 先按 keycode 找（没有 keysym 的键只能这样接），再按 keysym 找。 */
static int keymap_action(KeySym k, unsigned code) {
    int i;
    for (i = 0; i < keymap_n; i++)
        if (keymap[i].code && keymap[i].code == code) return keymap[i].act;
    for (i = 0; i < keymap_n; i++)
        if (!keymap[i].code && keymap[i].sym == k) return keymap[i].act;
    return ACT_NONE;
}
static void logline(const char *s) {
    if (!inputlog) return;
    fprintf(inputlog, "%lld %s\n", now_ms(), s);
    fflush(inputlog);
}

/* ═══════════════════════ 库管理 / 混合 ══════════════════════════════ */
static int find_key(const char *k) {
    int i;
    if (!k || !*k) return -1;
    for (i = 0; i < count; i++) if (!strcmp(recipes[i].key, k)) return i;
    return -1;
}
/* 引擎调用统一走 /bin/sh <engine> <verb> [args...]：与 apply/off 同一条
 * 链，不要求脚本带可执行位。refresh 说明这次操作会不会改变库内容：
 *   0 = 不改库（混合/还原）  1 = LUT 库（要重跑 list）  2 = 重读配方表 */
static int run_engine(const char *verb, const char *a1, const char *a2,
                      const char *a3, int refresh) {
    return run_engine4(verb, a1, a2, a3, NULL, refresh);
}
static int run_engine4(const char *verb, const char *a1, const char *a2,
                       const char *a3, const char *a4, int refresh) {
    char cmd[700];
    if (a4) snprintf(cmd, sizeof cmd, "sh %s %s %s %s %s %s", engine, verb, a1, a2, a3, a4);
    else if (a3) snprintf(cmd, sizeof cmd, "sh %s %s %s %s %s", engine, verb, a1, a2, a3);
    else if (a2) snprintf(cmd, sizeof cmd, "sh %s %s %s %s", engine, verb, a1, a2);
    else if (a1) snprintf(cmd, sizeof cmd, "sh %s %s %s", engine, verb, a1);
    else snprintf(cmd, sizeof cmd, "sh %s %s", engine, verb);
    refresh_after = refresh;
    if (!start_child(2, -1, cmd)) { refresh_after = 0; return 0; }
    return 1;
}
static int busy_guard(void) {
    if (child) { fprintf(stderr, "LIB busy kind=%d, request ignored\n", busy_kind);
                 message("正在执行 · 请稍候"); return 1; }
    return 0;
}
/* 下架：两段确认。LUT 是"移到 _trash"（可找回），配方是物理删除（有备份），
 * 但两者都不该被一次误触完成。 */
static void lib_delete(int idx) {
    if (idx < 0 || idx >= count || busy_guard()) return;
    if (!confirm_del) {
        confirm_del = 1; confirm_del_at = now_ms();
        snprintf(status, sizeof status, "%s下架「%s」？再点一次删除确认",
                 lut_mode ? "" : "永久删除配方「", lut_mode ? "" : recipes[idx].label);
        if (!lut_mode) snprintf(status, sizeof status, "永久删除「%s」？再点一次确认（有备份）", recipes[idx].label);
        dirty = 1;
        return;
    }
    confirm_del = 0;
    if (lut_mode) {
        if (run_engine("del", recipes[idx].key, NULL, NULL, 1)) message("正在移到 _trash…");
    } else if (run_engine("lib", "del", recipes[idx].key, NULL, 2)) message("正在删除配方…");
}
static void lib_move(int idx, int delta) {
    const char *dir = delta < 0 ? "up" : "down";
    if (idx < 0 || idx >= count || busy_guard()) return;
    if (lut_mode) {
        if (run_engine("move", recipes[idx].key, dir, NULL, 1)) message("正在调整顺序…");
    } else if (run_engine("lib", "move", recipes[idx].key, dir, 2)) message("正在调整顺序…");
}
/* 两个强度 → 一条 blend 命令。换算在这里做，能一次性把边界都挡住：
 * 两个 0 没有意义、A/B 同张没有意义、总和超 100 由 lut.sh 的归一处理。 */
/* 两个强度 → (比例 r, 总强度 s)。状态行和真正下发都用它，避免两处算法漂移。 */
static void mix_ratio(int *r, int *s) {
    int sum = mix_ra + mix_rb;
    if (sum <= 0) { *r = 0; *s = 0; return; }
    *r = (mix_rb * 100 + sum / 2) / sum;     /* B 的占比：0=全 A，100=全 B */
    *s = sum > 100 ? 100 : sum;              /* 总和超 100 就饱和，不外推 */
}

static void mix_apply(void) {
    char rbuf[8], sbuf[8];
    int r, s;
    if (!lut_mode) { message("混合只在 LUT 面板可用"); return; }
    if (mix_a < 0 || mix_b < 0) { message("先在浏览态长按标记 A 和 B"); return; }
    if (mix_a == mix_b) { message("A 和 B 是同一张 · 再标一个别的"); return; }
    if (mix_ra + mix_rb <= 0) { message("两个强度都是 0 · 没有可混合的"); return; }
    mix_ratio(&r, &s);
    if (busy_guard()) return;
    snprintf(rbuf, sizeof rbuf, "%d", r);
    snprintf(sbuf, sizeof sbuf, "%d", s);
    if (run_engine4("blend", recipes[mix_a].key, rbuf, recipes[mix_b].key, sbuf, 0)) {
        /* 混合表的登记名是 blend:A+B，选中项不再是"当前生效的那张表" */
        queue.applied = -1; queue.desired = -1;
        snprintf(status, sizeof status, "正在混合 %s × %s（比例 %d%% · 强度 %d%%）…",
                 recipes[mix_a].label, recipes[mix_b].label, r, s);
        dirty = 1;
    }
}
/* 把 recipes[] 写回面板读的那份扁平表。只在 LUT 模式用：配方的
 * recipes.txt 是 filmlab.sh export 的产物，面板不该抢着写。 */
static void write_recipes_file(void) {
    char tmp[300], line[256];
    FILE *f;
    int i;
    if (!lut_mode || !recipes_path_global) return;
    snprintf(tmp, sizeof tmp, "%s.tmp", recipes_path_global);
    if (!(f = fopen(tmp, "w"))) return;
    for (i = 0; i < count; i++) {
        snprintf(line, sizeof line, "%s|%s|0|0|0|0|0|0|0\n", recipes[i].key, recipes[i].label);
        fputs(line, f);
    }
    if (fclose(f) == 0) {
        if (rename(tmp, recipes_path_global) != 0) unlink(tmp);
    }
}
/* LUT 库的列表来自 `lut.sh list` 的输出。只认名字行：OK/NEXT/ERR/WARN
 * 这些是同一套脚本的动作回执，混进来会变成"叫 OK 的 look"。 */
static int rebuild_lut_list(const char *text) {
    static struct nx_recipe tmp[NXUI_MAX_RECIPES];
    int n = 0;
    const char *p = text;
    char keep[96];
    int keep_idx = (selected >= 0 && selected < count) ? selected : 0;
    snprintf(keep, sizeof keep, "%s", count > 0 ? recipes[keep_idx].key : "");
    while (p && *p && n < NXUI_MAX_RECIPES) {
        size_t len = strcspn(p, "\n"), copy = len;
        if (copy >= sizeof tmp[n].key - 1) copy = sizeof tmp[n].key - 1;
        if (len > 0) {
            memcpy(tmp[n].key, p, copy); tmp[n].key[copy] = 0;
            tmp[n].key[strcspn(tmp[n].key, "\r")] = 0;
        }
        p += len; if (*p) p++;
        if (!tmp[n].key[0]) continue;
        if (!strncmp(tmp[n].key, "OK ", 3) || !strncmp(tmp[n].key, "ERR ", 4) ||
            !strncmp(tmp[n].key, "NEXT ", 5) || !strncmp(tmp[n].key, "WARN ", 5) ||
            !strncmp(tmp[n].key, "HINT ", 5) || tmp[n].key[0] == '#') continue;
        if (!nx_key_valid(tmp[n].key)) continue;
        memset(tmp[n].label, 0, sizeof tmp[n].label);
        strcpy(tmp[n].label, tmp[n].key);
        n++;
    }
    if (n <= 0) { fprintf(stderr, "REFRESH lut: empty list, keeping %d entries\n", count); return 0; }
    memcpy(recipes, tmp, (size_t)n * sizeof *recipes);
    count = n;
    selected = find_key(keep);
    if (selected < 0) selected = 0;
    mix_a = find_key(mix_key_a);
    mix_b = find_key(mix_key_b);
    write_recipes_file();
    fprintf(stderr, "REFRESH lut: entries=%d sel=%d mix=%d/%d\n", count, selected, mix_a, mix_b);
    return 1;
}
static int reload_recipes_file(void) {
    static struct nx_recipe tmp[NXUI_MAX_RECIPES];
    char keep[96];
    int n, keep_idx = (selected >= 0 && selected < count) ? selected : 0;
    snprintf(keep, sizeof keep, "%s", count > 0 ? recipes[keep_idx].key : "");
    n = nx_load_recipes(recipes_path_global, tmp, NXUI_MAX_RECIPES);
    if (n <= 0) { fprintf(stderr, "REFRESH recipes: reload failed (%d)\n", n); return 0; }
    memcpy(recipes, tmp, (size_t)n * sizeof *recipes);
    count = n;
    selected = find_key(keep);
    if (selected < 0) selected = 0;
    mix_a = find_key(mix_key_a);
    mix_b = find_key(mix_key_b);
    fprintf(stderr, "REFRESH recipes: entries=%d sel=%d\n", count, selected);
    return 1;
}

/* 混合态进出：键盘（F2，仅桌面）与顶栏按钮共用。进态时给 A/B 一个可用起点，
 * 否则用户要先想「设成哪两张」才敢动滚条。 */
/* 进出混合态。退出**不**自动应用 —— 应用是按钮行第三格的事，用户可能只是想
 * 进去看一眼两个强度、再退回浏览。 */
static void toggle_mix(void) {
    if (!lut_mode) { message("混合只在 LUT 面板可用"); return; }
    if (ui_mode == UI_MIX) {
        ui_mode = UI_BROWSE;
        mix_focus = 0;
        confirm_del = 0;
        message("已退出混合（未应用）");
        dirty = 1;
        return;
    }
    ui_mode = UI_MIX;
    mix_focus = 0;
    confirm_del = 0;
    /* 没标记过就给个能用的起点，但标记权仍在浏览态的长按上 */
    if (mix_a < 0 && count > 0) {
        mix_a = selected;
        snprintf(mix_key_a, sizeof mix_key_a, "%s", recipes[mix_a].key);
    }
    if (mix_b < 0 && count > 1) {
        mix_b = (selected + 1 < count) ? selected + 1 : 0;
        if (mix_b == mix_a) mix_b = (mix_a + 1) % count;
        snprintf(mix_key_b, sizeof mix_key_b, "%s", recipes[mix_b].key);
    }
    message("左右键切 A/B · 滑杆或拨盘调强度 · 设好按应用混合");
    dirty = 1;
}

/* 调当前焦点的强度。混合态的滑杆与右上拨盘走同一条路，避免两处逻辑漂移。 */
static void bump_strength(int delta) {
    int *v = mix_focus ? &mix_rb : &mix_ra;
    int nv = *v + delta;
    if (nv < 0) nv = 0;
    if (nv > 100) nv = 100;
    if (nv == *v) return;
    *v = nv;
    dirty = 1;
}

/* 按键/触摸的语义分发：把「哪个键」和「做什么」解耦，键位表才好换。 */
static void do_action(int act) {
    if (act == ACT_NONE) return;
    /* 对话框是模态：任何退出类按键先把它收掉，不让按键穿到底下 */
    if (dialog_on) { dialog_on = 0; dirty = 1; return; }
    if (act == ACT_CLOSE) { queue.close_requested = 1; return; }
    /* 换层键在菜单页也认：菜单 → 进 LUT 层（入口语义不变，只是少按一次）。
     * 它必须在 `if (menu) return` 之前 —— 否则在菜单页按 WiFi 什么都不会发生。 */
    if (act == ACT_SWITCH) {
        if (menu) {
            snprintf(return_menu, sizeof return_menu, "%s", menu_path);
            menu = 0; set_layer(L_LUT); resize_panel();
        } else set_layer(lut_mode ? L_PW : L_LUT);
        dirty = 1;
        return;
    }
    if (menu) return;                 /* 菜单模式不抢按键，行为与旧版一致 */
    switch (act) {
    case ACT_PREV: choose(selected - 1); break;
    case ACT_NEXT: choose(selected + 1); break;
    /* 左右键：混合态选 A/B 焦点，其余态就是翻页。上下键始终翻页。 */
    case ACT_LEFT:
        if (ui_mode == UI_MIX) { mix_focus = 0; message("正在调 A"); break; }
        choose(selected - 1); break;
    case ACT_RIGHT:
        if (ui_mode == UI_MIX) { mix_focus = 1; message("正在调 B"); break; }
        choose(selected + 1); break;
    case ACT_EDIT:
        /* 编辑态的删除/排序走 `lut.sh del|move`，那两个动词只认标准库目录；
         * vlog 表是另一本目录，库里那套管理动词不认它，进去只会失败。 */
        if (lut_mode && lut_scope) { message("V-Log 表的增删排序用命令行 lut.sh"); break; }
        ui_mode = (ui_mode == UI_EDIT) ? UI_BROWSE : UI_EDIT;
        confirm_del = 0;
        message(ui_mode == UI_EDIT ? "编辑：删除 / 左移 / 右移 / 完成" : "已退出编辑");
        break;
    case ACT_MIX: toggle_mix(); return;
    case ACT_DEL:    if (ui_mode == UI_EDIT) lib_delete(selected); break;
    case ACT_MOVE_L: if (ui_mode == UI_EDIT) lib_move(selected, -1); break;
    case ACT_MOVE_R: if (ui_mode == UI_EDIT) lib_move(selected, +1); break;
    /* 右上拨盘（XF86ScrollUp/Down）：混合态调强度，其余态当翻页。
     * 步进 5% 是刻意的 —— 转盘每格一个 KeyPress，1% 步进要拧 100 格才走完，
     * 5% 是「拧三下能看出差别、又不至于跳过头」的折中（可辨粒度约 10%）。 */
    case ACT_INC:
        if (ui_mode == UI_MIX) { bump_strength(+5); break; }
        choose(selected + 1); break;
    case ACT_DEC:
        if (ui_mode == UI_MIX) { bump_strength(-5); break; }
        choose(selected - 1); break;
    case ACT_OK:
        if (ui_mode == UI_MIX) { mix_apply(); break; }
        if (ui_mode == UI_EDIT) { ui_mode = UI_BROWSE; message("已退出编辑"); break; }
        if (!child && count > 0) {
            nx_queue_select_now(&queue, selected, now_ms());
            queue.commit_requested = 1; queue.close_requested = 1;
        }
        break;
    }
    dirty = 1;
}
static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out; char buf[4096]; size_t n; int ok = 1;
    if (!in) return 0;
    out = fopen(to, "wb");
    if (!out) { fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;
    fclose(in); if (fclose(out) != 0) ok = 0;
    if (ok && chmod(to, 0755) != 0) ok = 0;
    if (!ok) unlink(to);
    return ok;
}
static void activate_item(int index) {
    struct nx_item *it = &items[index];
    char source[NXUI_PATH], target[NXUI_PATH], command[1024];
    const char *name = base_name(it->command); size_t i;
    fprintf(stderr, "ACTION item=%d checkbox=%d\n", index, it->checkbox);
    if (child) { message("请等待当前操作完成"); return; }
    if (strstr(it->command, "gui_exit.sh")) { queue.close_requested = 1; return; }
    /* 进程内换层。菜单文件里直接写 `filter:lut` / `filter:pw`，nxui 自己
     * 认这个伪命令 —— 不再 fork `nxui.sh switch`，省掉 1 秒黑屏，也从根上
     * 没有「pidof 命中正在退出的自己」那类竞态。 */
    if (!strncmp(it->command, "filter:", 7)) {
        int want = !strcmp(it->command + 7, "lut") ? L_LUT : L_PW;
        fprintf(stderr, "ACTION filter=%s from=%s\n", it->command + 7, menu_path);
        snprintf(return_menu, sizeof return_menu, "%s", menu_path);
        menu = 0; set_layer(want); resize_panel(); dirty = 1; return;
    }
    if (it->command[0] == '@') {
        if (strstr(it->command, "gui_filmlab1b")) { menu = 0; resize_panel(); return; }
        if (live && (!strncmp(it->command, "@/tmp/gui_br.", 13) ||
                     !strncmp(it->command, "@/tmp/gui_ramp.", 15))) {
            /* The legacy generator runs six global syncs. These volatile menu
             * files need no disk flush, and need not delay every UI opening. */
            snprintf(pending_menu, sizeof pending_menu, "%s", it->command);
            if (!start_child(3, -1, "sed '/^sync;/d' ./br_menu.sh | /bin/bash"))
                message("无法生成子菜单");
            return;
        }
        load_menu(it->command); return;
    }
    if (!strcmp(it->command, " ") || !*it->command) return;
    /* A menu operation supersedes any browsing request not yet dispatched. */
    queue.desired = queue.applied = -1;
    if (!it->checkbox && !strcmp(name, "telnet_toggle.sh")) {
        if (!live) { message("演示模式 · 未更改服务"); return; }
        refresh_services(1);
        if (!services.known) { message("无法读取服务状态 · 请重试"); return; }
        snprintf(command, sizeof command, "./telnet_toggle.sh --ui %s", services.telnet && services.ftp ? "disable" : "enable");
        if (start_child(4, -1, command)) message(services.telnet && services.ftp ? "正在关闭 Telnet/FTP…" : "正在开启 Telnet/FTP…");
        else message("服务操作启动失败");
        return;
    }
    if (!it->checkbox) { start_child(2, -1, it->command); return; }
    /* Legacy checkbox semantics: auto/<script> is a copy, off_<script> stops it. */
    for (i = 0; name[i]; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
            (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-' || name[i] == '.')) return;
    if (!i || strstr(name, "..") || !strcmp(name, "a_init.sh")) return;
    if (!live) { message("演示模式 · 未更改开关"); return; }
    snprintf(target, sizeof target, "%s/auto/%s", root, name);
    if (checked(name)) {
        if (unlink(target) != 0) { message("无法关闭开关"); return; }
        snprintf(source, sizeof source, "%s/off_%s", root, name);
        snprintf(command, sizeof command, "./off_%s", name);
        if (access(source, R_OK) == 0) start_child(2, -1, command);
        else message("已关闭自动执行");
    } else {
        snprintf(source, sizeof source, "%s/%s", root, name);
        snprintf(command, sizeof command, "./auto/%s", name);
        if (!copy_file(source, target)) { fprintf(stderr, "COPY failed errno=%d\n", errno); message("无法启用开关"); return; }
        start_child(2, -1, command);
    }
    dirty = 1;
}
/* 横向滚动条：自己画、自己算比例，不用 nk_slider_float。
 *
 * Nuklear 的滑块只在「当前鼠标位置仍落在滑块矩形内」时才继续更新
 * （nk_slider_behavior → nk_input_has_mouse_click_down_in_rect 里对
 * visual_cursor 做 hover 判定）。滚轮式滑块本该"按下任意位置→拖到任意位置"，
 * 但 696px 轨道分 7 步时每步 99px、滑块只有 26px —— 手指移出滑块的一瞬间
 * 拖动就断了：表现为按住能高亮、一拖就没反应。触摸屏上这条路径等于不可用。
 *
 * 这里改成：按下即跳到该比例（tap-to-jump），按住移动每帧按绝对位置重算，
 * 只有目标序号真的变了才返回，避免同像素重复 choose。 */
static int bar_drag;   /* 左键是否在滚动条上按着（面板只有一扇窗，静态即可） */
/* m1/m2 = 要在轨道上标出的两个位置（A/B）。跨页时用户不用记「我刚才标的是哪一项」，
 * 两个小刻度一直画在那里。混合态的强度滑杆不需要刻度，传 -1 即可。 */
static int scrollbar_row(struct nk_context *c, int sel, int n, int m1, int m2)
{
    struct nk_rect b, track, fill, knob;
    struct nk_command_buffer *cv;
    float ratio, span;
    const float kw = 30.0f;   /* 滑块宽：触摸目标，别按鼠标习惯做小 */
    int want, down, was;
    enum nk_widget_layout_states st = nk_widget(&b, c);
    {
        static int dumped;
        if (!dumped) { dumped = 1; fprintf(stderr, "BAR rect=%.0f,%.0f %.0fx%.0f st=%d\n", b.x, b.y, b.w, b.h, (int)st); }
    }
    if (st == NK_WIDGET_INVALID || n <= 0) return -1;
    cv = nk_window_get_canvas(c);
    /* 轨道 8px、行高 16px：上下各留 4px，触摸判定仍按整行 16px。 */
    track = nk_rect(b.x, b.y + (b.h - 8.0f) * 0.5f, b.w, 8.0f);
    ratio = (n > 1) ? (float)sel / (float)(n - 1) : 0.0f;
    down = nk_input_is_mouse_down(&c->input, NK_BUTTON_LEFT);
    was = bar_drag;
    if (nk_input_is_mouse_pressed(&c->input, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(&c->input, b))
        bar_drag = 1;
    want = -1;
    /* 抬手那一帧也要算：面板 10Hz，20ms 一次的 motion 会被合并，松手时
     * 鼠标早已在终点、但中间某帧才是最后处理到的位置。不按抬手位置补一刀，
     * 拖到最右端会停在半路（实测 480px 只到第 3 项）。 */
    if (bar_drag || (was && !down)) {
        /* 滑块中心跟随手指：两端留半个滑块，0 与 n-1 才真的贴边。 */
        span = track.w - kw;
        if (span < 1.0f) span = 1.0f;
        ratio = (c->input.mouse.pos.x - track.x - kw * 0.5f) / span;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        if (n > 1) want = (int)(ratio * (float)(n - 1) + 0.5f);
    }
    if (!down) bar_drag = 0;
    nk_fill_rect(cv, track, 4.0f, nk_rgb(45, 53, 59));
    fill = nk_rect(track.x, track.y, ratio * track.w, track.h);
    if (fill.w > 0.0f) nk_fill_rect(cv, fill, 4.0f, nk_rgb(88, 111, 79));
    /* A/B 刻度画在轨道之上、滑块之下：比轨道高 6px，拇指也能看见。 */
    if (n > 1) {
        struct nk_color mark[2];
        int mm[2], k;
        mark[0] = nk_rgb(203, 233, 153);   /* A：与面板强调色一致 */
        mark[1] = nk_rgb(235, 176, 122);   /* B：暖橙，和 A 一眼分得开 */
        mm[0] = m1; mm[1] = m2;
        for (k = 0; k < 2; k++) {
            float mx;
            if (mm[k] < 0 || mm[k] >= n) continue;
            mx = track.x + (track.w - 3.0f) * ((float)mm[k] / (float)(n - 1));
            nk_fill_rect(cv, nk_rect(mx, track.y - 3.0f, 3.0f, track.h + 6.0f),
                         1.0f, mark[k]);
        }
    }
    knob.w = kw; knob.h = 12.0f;
    knob.x = track.x + ratio * (track.w - kw);
    knob.y = b.y + (b.h - knob.h) * 0.5f;
    nk_fill_rect(cv, knob, 4.0f, nk_rgb(203, 233, 153));
    if (want >= 0 && want != sel) {
        fprintf(stderr, "BAR sel=%d want=%d x=%.0f\n", sel, want, c->input.mouse.pos.x);
        return want;
    }
    return -1;
}
static void draw(void) {
    char line[320]; int i, start;
    struct nk_color accent = nk_rgb(203, 233, 153);
    if (nk_begin(ctx, "NXWA", nk_rect(0, 0, (float)width, (float)height), NK_WINDOW_NO_SCROLLBAR)) {
        /* 顶栏四格（356+104×3 + 间距 ≈ 680 < 面板内宽 704）。
         * 第三格是「混合」入口：机身实测没有 F2 这类键（2026-10-17 双探针），
         * 只有触摸，所以混合态必须有一个手指够得到的入口，否则等于没交付。
         * 非 LUT 模式用空 label 占位 —— 行是手动 push 的，格数必须对满。 */
        if (menu) {
            /* 菜单页：EV+WiFi 打开的第一层。右上角那个「滤镜」是回到上次
             * 所在滤镜层的快捷方式，不是第二个入口 —— 两个页签在滤镜页里。 */
            nk_layout_row_begin(ctx, NK_STATIC, 24, 4);
            nk_layout_row_push(ctx, 300);
            nk_label_colored(ctx, "NX-KS  /  菜单", NK_TEXT_LEFT, accent);
            nk_layout_row_push(ctx, 96);
            /* 「返回 / 取消」常驻顶栏 —— 原来它们是菜单文件里的最后两项，
             * 排在末页，翻两页才够得着（用户明确否掉这一点）。 */
            if (nk_button_label(ctx, "返回")) {
                if (!strstr(menu_path, "gui_ini.NX500") && load_menu("gui_ini.NX500")) dirty = 1;
                else queue.close_requested = 1;
            }
            nk_layout_row_push(ctx, 96);
            if (nk_button_label(ctx, "滤镜")) {
                snprintf(return_menu, sizeof return_menu, "%s", menu_path);
                menu = 0; resize_panel();
            }
            nk_layout_row_push(ctx, 96);
            if (nk_button_label(ctx, "关闭")) queue.close_requested = 1;
            nk_layout_row_end(ctx);
        } else {
            /* 滤镜页：两个页签就是两层。点页签 = 进程内换层，不重启进程，
             * 也不碰任何一层的相机状态（LUT 走 A18，PW 走 prefman）。 */
            /* 页签用「有色标签 + ●」表示当前层，另一层才是按钮：
             * nk_selectable_label 的选中态只是换文字颜色，在这套深色皮肤上
             * 几乎看不出来（实测两页签长得一模一样），所以不用它。 */
            nk_bool on_lut = (cur == &layers[L_LUT]);
            /* 宽度对调（LUT 132→152、FILMLAB 152→132）：LUT 页签在 vlog 域要
             * 写 "● LUT·VLOG"，132 减去两侧 padding 只剩 116，正好卡在边缘；
             * FILMLAB 8 个字符在 132 里仍有富余。总宽 284 不变。 */
            nk_layout_row_begin(ctx, NK_STATIC, 24, 5);
            nk_layout_row_push(ctx, 152);
            /* ★ 当前层的页签也必须是按钮：再点一次 = 换 LUT 的输入域
             *   （std ⇄ vlog），vlog 表因此不需要第三个页签。
             *   选中态靠把强调色临时塞进按钮的三种文字色来画 —— 早先用
             *   nk_label_colored 是因为 nk_selectable_label 的选中态只换
             *   文字色、在这套深色皮肤上看不出来；可 label 没有命中区，
             *   「再点一次」就落空了。 */
            {
                const char *tab = !on_lut ? "LUT" : (lut_scope ? "● LUT·VLOG" : "● LUT");
                struct nk_color tn = ctx->style.button.text_normal,
                                 th = ctx->style.button.text_hover,
                                 ta = ctx->style.button.text_active;
                if (on_lut) {
                    ctx->style.button.text_normal = accent;
                    ctx->style.button.text_hover = accent;
                    ctx->style.button.text_active = accent;
                }
                if (nk_button_label(ctx, tab)) {
                    if (on_lut) toggle_lut_scope();
                    else set_layer(L_LUT);
                }
                if (on_lut) {
                    ctx->style.button.text_normal = tn;
                    ctx->style.button.text_hover = th;
                    ctx->style.button.text_active = ta;
                }
            }
            nk_layout_row_push(ctx, 132);
            if (!on_lut) nk_label_colored(ctx, "● FILMLAB", NK_TEXT_CENTERED, accent);
            else if (nk_button_label(ctx, "FILMLAB")) set_layer(L_PW);
            /* 混合只属于 LUT 层。PW 层也留着这个按钮：换层后入口凭空消失
             * 会让人以为功能没了，点了给一句说明比藏起来好。 */
            nk_layout_row_push(ctx, 124);
            if (nk_button_label(ctx, ui_mode == UI_MIX ? "退出混合" : "混合")) toggle_mix();
            nk_layout_row_push(ctx, 126);
            if (nk_button_label(ctx, "菜单")) {
                /* 回进滤镜前的那个菜单页（通常是 gui_evwifi），不是主菜单 */
                load_menu(return_menu[0] ? return_menu : "gui_ini.NX500");
            }
            nk_layout_row_push(ctx, 126);
            if (nk_button_label(ctx, "关闭")) queue.close_requested = 1;
            nk_layout_row_end(ctx);
        }
        if (menu) {
            start = page * MENU_PAGE;
            nk_layout_row_dynamic(ctx, 40, MENU_COLS);
            for (i = start; i < start + MENU_PAGE; i++) {
                if (i >= item_count) { nk_label(ctx, "", NK_TEXT_LEFT); continue; }
                if (!strcmp(base_name(items[i].command), "telnet_toggle.sh"))
                    nx_services_label(&services, line, sizeof line);
                else snprintf(line, sizeof line, "%s%s", items[i].checkbox ? (checked(items[i].command) ? "[开] " : "[关] ") : "", items[i].label);
                if (nk_button_label(ctx, line)) { activate_item(i); break; }
            }
            nk_layout_row_dynamic(ctx, 32, MENU_COLS);
            if (nk_button_label(ctx, "上一页")) { page = page > 0 ? page - 1 : (item_count-1)/MENU_PAGE; dirty = 1; }
            snprintf(line, sizeof line, "%d / %d", page + 1, (item_count+MENU_PAGE-1)/MENU_PAGE);
            nk_label(ctx, line, NK_TEXT_CENTERED);
            if (nk_button_label(ctx, "下一页")) { page = (page+1) % ((item_count+MENU_PAGE-1)/MENU_PAGE); dirty = 1; }
        } else if (count > 0) {
            int gi, win = 5, first, idx;
            /* 60px：面板 208px 高，标题/滤镜条/状态行/滚动条/按钮行/底部状态
             * 六层必须放得下。84px 时最后两行掉出面板底部、静默不渲染，
             * "保存并关闭"和"还原原厂"整行消失过。 */
            nk_layout_row_dynamic(ctx, 60, win);
            first = selected - win / 2;
            if (first < 0) first = 0;
            if (first > count - win) first = count - win;
            if (first < 0) first = 0;
            for (gi = 0; gi < win; gi++) {
                idx = first + gi;
                /* ★ 空位必须用 nk_label 占，不能用 nk_spacing(1)：
                 * nk_spacing 在「最后一列」上会把 row.index 绕回 0 并
                 * nk_panel_alloc_row —— 等于多开一整行 65px。库内条目 < 5 时
                 * 按钮行被顶到 y=188，整行掉出 208px 面板（静默不渲染）。
                 * 只有"库里只剩几张表"才会触发，常规 40+ 张时永远看不到。 */
                if (idx >= count) { nk_label(ctx, "", NK_TEXT_LEFT); continue; }
                nk_bool picked = (idx == selected);
                /* MIX 模式下在条目上标 [A]/[B]：否则用户看不出混的是哪两张 */
                snprintf(line, sizeof line, "%s%s%s", idx == selected ? "● " : "",
                         (lut_mode && idx == mix_a) ? "[A] " :
                         (lut_mode && idx == mix_b) ? "[B] " : "",
                         recipes[idx].label);
                /* A tap always applies, including on the highlighted row: the
                 * camera or the legacy menu may have changed ISP state since
                 * the preview, so the marker is not proof the value is live.
                 * Nuklear reports a press once and flips *picked, so the old
                 * "&& idx != selected" guard made the highlighted row inert. */
                if (nk_selectable_label(ctx, line, NK_TEXT_CENTERED, &picked))
                    choose(idx);
            }
            /* LUT mode has no PW components -- showing R0 G0 B0 would be noise,
             * and the 16px it costs is part of what pushes the button row out. */
            if (!lut_mode) {
                snprintf(line, sizeof line, "%d/%d   R%d G%d B%d   H%d S%d 锐%d 对比%d", selected+1, count,
                    recipes[selected].value[0], recipes[selected].value[1], recipes[selected].value[2],
                    recipes[selected].value[3], recipes[selected].value[4], recipes[selected].value[5], recipes[selected].value[6]);
                nk_layout_row_dynamic(ctx, 16, 1);
                nk_label_colored(ctx, line, NK_TEXT_LEFT, nk_rgb(171, 182, 190));
            }
            /* 横向滚动条（两模式共用）：整条宽度映射到 0..count-1，拖动直接改
             * 选中项，视图随 first = selected - win/2 一起滚。拖动期间的连续
             * choose 由 150ms 防抖收敛成一次 apply，不会每个像素写一次槽。
             * MIX 模式下同一条控件变成 0-100 的强度滑杆：手势与位置都没变，
             * 用户不用学第二套操作，也不多占一行高度。 */
            nk_layout_row_dynamic(ctx, 16, 1);
            if (ui_mode == UI_MIX) {
                /* 滑杆调「当前焦点那个」的强度：左右键切焦点，位置不变手感就一致 */
                int cur = mix_focus ? mix_rb : mix_ra;
                int want = scrollbar_row(ctx, cur, 101, -1, -1);
                if (want >= 0 && want != cur) {
                    if (mix_focus) mix_rb = want; else mix_ra = want;
                    dirty = 1;
                }
            } else {
                int want = scrollbar_row(ctx, selected, count,
                                         lut_mode ? mix_a : -1, lut_mode ? mix_b : -1);
                if (want >= 0 && want != selected) choose(want);
            }
            if (ui_mode == UI_EDIT) {
                /* 编辑态：按钮行整体换掉，不做成一排六个小按钮 ——
                 * 168px 一列是手指可点的下限，六个就只剩 112px。
                 * 「删除」是两段式：第一次只提示，第二次才执行；3 秒不动自动撤销。 */
                int b_del, b_ml, b_mr, b_done;
                nk_layout_row_dynamic(ctx, 26, 4);
                b_del  = nk_button_label(ctx, confirm_del ? "确认删除" : "删除");
                b_ml   = nk_button_label(ctx, "‹ 左移");
                b_mr   = nk_button_label(ctx, "右移 ›");
                b_done = nk_button_label(ctx, "完成");
                if (b_ml)   { confirm_del = 0; lib_move(selected, -1); }
                if (b_mr)   { confirm_del = 0; lib_move(selected, +1); }
                if (b_done) { confirm_del = 0; ui_mode = UI_BROWSE; message("已退出编辑"); }
                if (b_del)  lib_delete(selected);
            } else if (ui_mode == UI_MIX) {
                /* A/B 在这里设（长按已还给编辑态）。翻页靠滚条一次跨全库，
                 * 所以「跨度大」不是问题；强度靠滑杆 + 右上拨盘 + 左右键切焦点。 */
                nk_layout_row_dynamic(ctx, 26, 4);
                if (nk_button_label(ctx, "设为A") && count > 0) { mix_a = selected; mix_focus = 0;
                    snprintf(mix_key_a, sizeof mix_key_a, "%s", recipes[selected].key);
                    snprintf(status, sizeof status, "A = %s", recipes[selected].label); dirty = 1; }
                if (nk_button_label(ctx, "设为B") && count > 0) { mix_b = selected; mix_focus = 1;
                    snprintf(mix_key_b, sizeof mix_key_b, "%s", recipes[selected].key);
                    snprintf(status, sizeof status, "B = %s", recipes[selected].label); dirty = 1; }
                if (nk_button_label(ctx, "应用混合")) mix_apply();
                if (nk_button_label(ctx, "退出")) { ui_mode = UI_BROWSE; mix_focus = 0; message("已退出混合"); }
            } else if (lut_mode) {
                /* LUT 面板：apply 本身就是"保存"（写槽 + 登记表），所以
                 * "保存并关闭"只等队列里最后一张 drain 完再退出，没有额外 commit。
                 * 四个按钮与 FILMLAB 对齐：浏览 / 提交 / 复位 / 浏览。
                 * 顶栏那个"关闭"是 X 窗口的退出，不要在这里重复放第三个。 */
                nk_layout_row_dynamic(ctx, 26, 4);
                if (nk_button_label(ctx, "‹ 上一个")) choose(selected-1);
                if (nk_button_label(ctx, "保存并关闭") && !queue.close_requested) {
                    queue.close_requested = 1;
                }
                if (nk_button_label(ctx, "还原原厂") && !queue.close_requested) {
                    if (child) message("正在执行 · 请稍候");
                    else {
                        char restore_cmd[1024];
                        snprintf(restore_cmd, sizeof restore_cmd, "%s off", engine);
                        if (start_child(8, -1, restore_cmd)) {
                            queue.applied = -1;
                            queue.desired = -1;
                            message("正在还原原厂表…");
                        } else message("还原启动失败");
                    }
                }
                if (nk_button_label(ctx, "下一个 ›")) choose(selected+1);
            } else {
                /* 四个按钮。顺序：浏览 / 提交 / 复位 / 浏览 —— "保存并关闭" 是主
                 * 操作，留在第二列（视觉中心附近），"还原" 是低频动作放第三列。
                 * 还原只把中性值直推给 ISP，不写槽、不落盘，与预览同语义：
                 * 所以"保存并关闭"之后仍会 apply 当前配方；要让复位持久只能靠
                 * 相机自己的风格复位。 */
                nk_layout_row_dynamic(ctx, 26, 4);
                if (nk_button_label(ctx, "‹ 上一个")) choose(selected-1);
                if (nk_button_label(ctx, "保存并关闭") && !queue.close_requested) {
                    if (!child && selected == queue.applied) {
                        /* 当前选中已预览生效：仅保存到闪存，免一次完整 apply */
                        close_after_child = 1;
                        if (!start_child(2, -1, "prefman save 0")) { close_after_child = 0; message("保存启动失败"); }
                    } else {
                        nx_queue_select_now(&queue, selected, now_ms());
                        queue.commit_requested = 1;
                        queue.close_requested = 1;
                    }
                }
                if (nk_button_label(ctx, "还原") && !queue.close_requested) {
                    if (child) message("正在执行 · 请稍候");
                    else {
                        char reset_cmd[1024];
                        snprintf(reset_cmd, sizeof reset_cmd, "%s neutral", engine);
                        if (start_child(7, -1, reset_cmd)) {
                            queue.applied = -1;
                            queue.desired = -1;
                            message("正在还原中性…");
                        } else message("还原启动失败");
                    }
                }
                if (nk_button_label(ctx, "下一个 ›")) choose(selected+1);
            }
        } else {
            /* Say where to look. The generic message cost an hour of guessing
             * once: three uppercase look names had silently voided the list. */
            nk_layout_row_dynamic(ctx, 60, 1);
            nk_label(ctx, lut_mode ? "滤镜库为空 · 见 /tmp/nxui.log 与 /mnt/mmc/looks"
                                   : "配方表不可用 · 见 /tmp/nxui.log",
                     NK_TEXT_LEFT);
        }
        nk_layout_row_dynamic(ctx, 23, 1);
        /* 混合态把 A/B/强度放到底部状态行，而不是新开一行：LUT 面板的
         * 123/149 按钮行位置被测试钉死，多一行 16px + 5px 间距会把按钮行
         * 推出面板底部（旧版就这样静默丢掉过整行按钮）。 */
        if (ui_mode == UI_MIX && count > 0) {
            int rr, ss;
            mix_ratio(&rr, &ss);
            snprintf(line, sizeof line,
                "A %d%%   B %d%%   -> 比例 %d%% / 强度 %d%%",
                mix_ra, mix_rb, rr, ss);
            nk_label_colored(ctx, line, NK_TEXT_LEFT, accent);
        } else {
            nk_label_colored(ctx, live ? status : "演示模式 · 未连接相机", NK_TEXT_LEFT, nk_rgb(171, 182, 190));
        }
        /* ── 面板内横幅（2026-11-20）─────────────────────────────────
         * 为什么不用 popup_timeout：它是独立窗口，会被这块 override-redirect
         * 面板盖住 —— 用户看不到任何反馈（"点了没反应"）。面板自己画就一定
         * 在最上层，字号/配色也跟当前 UI 一致。
         * 由 /tmp/nxui-banner 驱动：一行 "<文字>\t<秒>"，画完即删。 */
        if (banner[0]) {
            const struct nk_user_font *f = ctx->style.font;
            struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
            float tw = f->width(f->userdata, f->height, banner, (int)strlen(banner));
            float bw = tw + 48, bh = 46;
            struct nk_rect r;
            if (bw > width - 40) bw = width - 40;
            r = nk_rect((width - bw) / 2, height / 2 - bh / 2, bw, bh);
            nk_fill_rect(cb, r, 8, nk_rgba(18, 22, 26, 242));
            nk_stroke_rect(cb, r, 8, 1.5f, accent);
            nk_draw_text(cb, nk_rect(r.x + 24, r.y + (bh - f->height) / 2 + 2, tw, f->height),
                         banner, (int)strlen(banner), f, nk_rgba(0, 0, 0, 0),
                         nk_rgb(232, 238, 226));
        }
        if (dialog_on) {
            const struct nk_user_font *f = ctx->style.font;
            struct nk_command_buffer *cb = nk_window_get_canvas(ctx);
            const char *lines[8];
            int nlines = 0, k = 0;
            const char *p2 = dialog;
            float maxw = 0;
            while (*p2 && nlines < 8) {
                const char *nl = strchr(p2, '\n');
                size_t len = nl ? (size_t)(nl - p2) : strlen(p2);
                float w = f->width(f->userdata, f->height, p2, (int)len);
                if (w > maxw) maxw = w;
                lines[nlines++] = p2;
                if (!nl) break;
                p2 = nl + 1;
            }
            {
                float bw = maxw + 56, bh = (float)nlines * 24 + 74;
                struct nk_rect r, btn;
                if (bw > width - 32) bw = width - 32;
                if (bh > height - 16) bh = height - 16;
                r = nk_rect((width - bw) / 2, (height - bh) / 2, bw, bh);
                nk_fill_rect(cb, r, 8, nk_rgba(16, 20, 24, 246));
                nk_stroke_rect(cb, r, 8, 1.5f, accent);
                for (k = 0; k < nlines; k++) {
                    const char *nl = strchr(lines[k], '\n');
                    int len = nl ? (int)(nl - lines[k]) : (int)strlen(lines[k]);
                    float w = f->width(f->userdata, f->height, lines[k], len);
                    float x = r.x + (bw - w) / 2;
                    float y = r.y + 18 + k * 24;
                    if (x < r.x + 12) x = r.x + 12;
                    if (w > bw - 24) len = (int)((bw - 24) / (f->height * 0.62f)); /* 粗略截断 */
                    nk_draw_text(cb, nk_rect(x, y, w, f->height), lines[k], len, f,
                                 nk_rgba(0, 0, 0, 0), nk_rgb(226, 233, 220));
                }
                btn = nk_rect(r.x + bw - 108, r.y + bh - 44, 92, 30);
                nk_fill_rect(cb, btn, 6, nk_rgba(38, 46, 40, 255));
                nk_stroke_rect(cb, btn, 6, 1.0f, accent);
                {
                    static const char lbl[] = "关闭";
                    float lw = f->width(f->userdata, f->height, lbl, 6);
                    nk_draw_text(cb, nk_rect(btn.x + (btn.w - lw) / 2, btn.y + (btn.h - f->height) / 2 + 2, lw, f->height),
                                 lbl, 6, f, nk_rgba(0, 0, 0, 0), accent);
                }
                dialog_close = btn;
            }
        }
    }
    nk_end(ctx);
    nk_xlib_render(window, nk_rgb(24, 29, 33));
    XFlush(display);
}
static void snapshot(const char *path) {
    XImage *img; FILE *f; int x, y;
    XSync(display, False);
    img = XGetImage(display, window, 0, 0, (unsigned)width, (unsigned)height, AllPlanes, ZPixmap);
    if (!img || !(f = fopen(path, "wb"))) { if (img) XDestroyImage(img); return; }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (y = 0; y < height; y++) for (x = 0; x < width; x++) {
        unsigned long p = XGetPixel(img, x, y);
        unsigned long masks[3] = {img->red_mask, img->green_mask, img->blue_mask}; int c;
        for (c = 0; c < 3; c++) {
            unsigned long m = masks[c], v = p & m;
            if (!m) { fputc(0, f); continue; }
            while (!(m & 1)) { m >>= 1; v >>= 1; }
            fputc((int)(v * 255 / m), f);
        }
    }
    fclose(f); XDestroyImage(img);
}
/* 按文件直接开字体，绕过 fontconfig 的字体列表。
 * 生产配置刻意只留子集字体的目录：fontconfig 扫描一个 <dir> 时会为目录里
 * 每个文件建 pattern，只要那份 10.7 MB 的 SDIC 还在搜索路径上，每次开面板
 * 都要白付 147 ms（bench_font_dirs.sh: mod-system 235 ms vs mod-only 52 ms）。
 * 代价是 family 名在配置里不再可见，缺字回退只能走文件。实测这条路径
 * 370 ms 左右，只在用户把 look 名写成 GB2312 以外的字时才会走到。 */
static XFont *font_open_file(Display *dpy, const char *path, double pixels)
{
    FcPattern *pat;
    XftFont *ft;
    XFont *f;
    if (!path || !*path) return NULL;
    pat = FcPatternCreate();
    if (!pat) return NULL;
    FcPatternAddString(pat, FC_FILE, (const FcChar8 *)path);
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, pixels);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    ft = XftFontOpenPattern(dpy, pat);   /* 成功与否 pattern 都归 Xft 管 */
    if (!ft) return NULL;
    f = calloc(1, sizeof *f);
    if (!f) { XftFontClose(dpy, ft); return NULL; }
    f->ft = ft;
    f->ascent = ft->ascent; f->descent = ft->descent; f->height = ft->height;
    return f;
}

/* 面板要显示的用户数据分两类：配方表（look 名）和菜单项。子集字体只覆盖
 * GB2312，而这两类都是用户自己起的名，不能假设字符范围。逐个码点问
 * XftCharExists，缺一个就换回系统字体：多花一次打开的钱，但不会出现空白。 */
static unsigned utf8_codepoint(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned c = *p++, cp;
    int n;
    if (c < 0x80) { *pp = p; return c; }
    if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
    else { *pp = p; return 0xFFFD; }
    while (n-- > 0) cp = (cp << 6) | (*p++ & 0x3Fu);
    *pp = p;
    return cp;
}

static int font_missing_cp;   /* 最近一次校验里第一个缺失码点，0 表示没缺 */

static int font_covers(const XFont *f, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!f || !f->ft) return 0;
    while (p && *p) {
        unsigned cp = utf8_codepoint(&p);
        if (cp == 0xFFFD) { font_missing_cp = cp; return 0; }
        /* FcCharSetHasChar, not XftCharExists: the latter asks the X server
         * whether the glyph is present in the *rendering* path, and returns 0
         * for every CJK codepoint on this Xft build even when the face has
         * them -- which silently demoted the GB2312 tier to the 10.7 MB stock
         * font and made the panel slower than before the optimization. */
        if (!f->ft->charset) { font_missing_cp = -1; return 0; }
        if (!FcCharSetHasChar(f->ft->charset, (FcChar32)cp)) { font_missing_cp = (int)cp; return 0; }
    }
    return 1;
}

static int font_covers_panel(const XFont *f)
{
    int i;
    for (i = 0; i < count; i++)
        if (!font_covers(f, recipes[i].label) || !font_covers(f, recipes[i].key)) return 0;
    for (i = 0; i < item_count; i++)
        if (!font_covers(f, items[i].label)) return 0;
    return 1;
}

static void last_choice_path(char *buf, size_t n, int L)
{
    snprintf(buf, n, "%s/last-%s", state_dir, L == L_LUT ? "lut" : "filmlab");
}

/* 启动时把 selected 移到上次那一项。找不到（库没了这条）就留在第一项 ——
 * 静默回到开头比卡在一个不存在的名字上安全。两层各存各的。 */
static void last_choice_load(int L)
{
    char path[256], line[sizeof last_choice], *nl;
    FILE *f;
    int i;
    if (!state_dir || !*state_dir) return;
    last_choice_path(path, sizeof path, L);
    if (!(f = fopen(path, "r"))) return;
    if (fgets(line, sizeof line, f)) {
        if ((nl = strchr(line, '\n')) != NULL) *nl = 0;
        for (i = 0; i < layers[L].n; i++) {
            if (!strcmp(layers[L].rec[i].key, line)) {
                layers[L].sel = i;
                if (L == (int)(cur - layers))
                    snprintf(last_choice, sizeof last_choice, "%s", line);
                break;
            }
        }
    }
    fclose(f);
}

static void last_choice_save(int L)
{
    char path[256], tmp[264];
    FILE *f;
    if (!state_dir || !*state_dir) return;
    if (layers[L].sel < 0 || layers[L].sel >= layers[L].n) return;
    last_choice_path(path, sizeof path, L);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "w"))) return;   /* 卡不在或只读：不影响面板 */
    fprintf(f, "%s\n", layers[L].rec[layers[L].sel].key);
    if (fclose(f) != 0) { unlink(tmp); return; }
    if (rename(tmp, path) != 0) unlink(tmp);
}

/* Font opening is the single largest startup cost: 200-240 ms of a ~290 ms
 * first frame (out/arm-*.log, qemu-arm against the camera's own Xft/FreeType).
 * It is neither the file size nor a fontconfig directory scan: the cache is
 * unusable here -- fontconfig never manages to leave a cache file on this
 * path, so every open rescans. The cost is the cmap walk that builds FcCharSet:
 *   stock SDIC_GP_US   10.7 MB, 44,426 codepoints -> 147 ms in FcFreeTypeQuery
 *   GB2312 subset       1.9 MB,  7,547 codepoints ->  22 ms
 *   panel subset         38 KB,    256 codepoints ->   1 ms
 * FT_New_Face alone is 1 ms, so glyf is already lazy; only cmap is walked.
 * Reproduce with bench_font.sh / bench_font_cache.sh, build with
 * make_font_subset.py.
 *
 * Two tiers by default, plus an opt-in third (measured, qemu-arm against the
 * camera's own Xft/FreeType, cold fontconfig cache):
 *   NXUI_CJK     1.9 MB,  7,547 codepoints -> 53 ms   all of GB2312 (default)
 *   SDIC (stock) 10.7 MB, 44,426 codepoints -> 370 ms  opened by file, not family
 * The default is the GB2312 subset: it covers every menu item and every look
 * name written in Simplified Chinese, and its rendering is byte-identical to
 * the stock face (same PPM hash), so a fallback changes speed, never looks.
 * A 38 KB panel-only subset was tried as an extra first tier and dropped:
 * opening it by file costs 50 ms (FcFreeTypeQuery's fixed cost dominates once
 * the codepoint set is small), which is no better than the GB2312 family.
 * Env overrides: NXUI_FONT_MIN (opt-in tier 0) / NXUI_FONT_GB / NXUI_FONT_FILE. */
int main(int argc, char **argv) {
    const char *initial_menu = NULL, *shot = NULL;
    const char *pw_recipes = "/mnt/mmc/filmlab/recipes.txt", *lut_recipes = "/tmp/nxui-look.rec";
    const char *lut_recipes_vlog = "/tmp/nxui-look-vlog.rec";
    const char *pw_engine = "/opt/usr/nx-ks/filmlab.sh", *lut_engine = "/opt/usr/nx-ks/lut.sh";
    int start_layer = L_PW, lut_alias = 0, lut_rec_given = 0, lut_eng_given = 0;
    int font_tier = 0;   /* 0=面板子集(文件) 1=GB2312 子集(family) 2=原厂(文件) */
    const char *font_min = getenv("NXUI_FONT_MIN");      /* 一级：按文件开，只够面板自身文本 */
    const char *font_gb = getenv("NXUI_FONT_GB");        /* 二级：GB2312 全集，按 family 开 */
    const char *font_name = "NXUI_CJK:pixelsize=19";
    const char *font_file = getenv("NXUI_FONT_FILE");    /* 三级：原厂 10.7 MB，按文件开 */
    int font_explicit = 0;
    XSetWindowAttributes attr; XFont *font; Atom wm_delete; int i, lockfd, render_count = 0;
    long long started, next_frame = 0, max_draw_ms = 0, process_started = now_ms();
    long long recipes_ready, display_ready, font_ready, backend_ready, font_final;
    int wakeups = 0, blits = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--live")) live = 1;
        else if (!strcmp(argv[i], "--dry-run")) live = 0;
        /* --lut / --tab lut 都只是「进来就停在 LUT 层」。两层永远都装，
         * 因为菜单页可以在进程内换层，装一半的进程一切过去就是空列表。 */
        else if (!strcmp(argv[i], "--lut")) { start_layer = L_LUT; lut_alias = 1; }
        else if (i+1 < argc && !strcmp(argv[i], "--tab"))
            start_layer = !strcmp(argv[++i], "lut") ? L_LUT : L_PW;
        else if (i+1 < argc && !strcmp(argv[i], "--root")) root = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--engine")) pw_engine = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--lut-engine")) { lut_engine = argv[++i]; lut_eng_given = 1; }
        else if (i+1 < argc && !strcmp(argv[i], "--recipes")) pw_recipes = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--lut-recipes")) { lut_recipes = argv[++i]; lut_rec_given = 1; }
        /* vlog 域是同一层的第二张清单，不是第二层：见 lut_scope 的注释。 */
        else if (i+1 < argc && !strcmp(argv[i], "--lut-recipes-vlog")) lut_recipes_vlog = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--menu")) initial_menu = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--font")) { font_name = argv[++i]; font_explicit = 1; }
        else if (i+1 < argc && !strcmp(argv[i], "--snapshot")) shot = argv[++i];
        else if (i+1 < argc && !strcmp(argv[i], "--lifetime-ms")) lifetime = atol(argv[++i]);
        else if (i+1 < argc && !strcmp(argv[i], "--bench-frames")) bench_frames = atoi(argv[++i]);
        else if (i+1 < argc && !strcmp(argv[i], "--inputlog")) inputlog_path = argv[++i];
        else { fprintf(stderr, "Usage: nxui [--live|--dry-run] [--tab lut|pw] [--root dir] [--recipes file] [--lut-recipes file] [--menu gui_file] [--engine file] [--lut-engine file] [--snapshot ppm] [--lifetime-ms n] [--inputlog file]\n"); return 2; }
    }
    /* 兼容旧调用（测试与老脚本）：`--lut --recipes X --engine Y` 里的这两个
     * 路径一直指的就是 LUT 层自己（那时只有一个层）。没单独给
     * --lut-recipes / --lut-engine 就沿用它们，否则 --lut 会变成
     * 「停在 LUT 层，但那一层是空的」。 */
    if (lut_alias) {
        if (!lut_rec_given) lut_recipes = pw_recipes;
        if (!lut_eng_given) lut_engine = pw_engine;
    }
    if (getenv("NXUI_NET_DIR")) net_dir = getenv("NXUI_NET_DIR"); /* isolated service-state tests */
    /* 缺字回退与"子集字体缺失"共用这一个文件路径；不同固件字体名可能不同，
     * 所以留了环境变量口子。 */
    if (!font_file || !*font_file) font_file = "/usr/share/fonts/SDIC_GP_US_20120720.ttf";
    if (!font_gb || !*font_gb) font_gb = "NXUI_CJK:pixelsize=19";
    /* 一级默认不开（NXUI_FONT_MIN 为空）：见字体打开处的实测对比。 */
    if (getenv("NXUI_FRAME_MS")) {
        int ms = atoi(getenv("NXUI_FRAME_MS"));
        if (ms >= 20 && ms <= 1000) frame_ms = ms;
    }
    /* A screenshot is always read-only, regardless of option order. */
    if (shot) live = 0;
    lockfd = open("/tmp/nxwa-nxui.lock", O_CREAT|O_RDWR, 0600);
    if (lockfd < 0 || flock(lockfd, LOCK_EX|LOCK_NB) != 0) return 3;
    fcntl(lockfd, F_SETFD, FD_CLOEXEC);
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    { struct sigaction action; memset(&action, 0, sizeof action); action.sa_handler = on_child;
      sigemptyset(&action.sa_mask); action.sa_flags = SA_NOCLDSTOP; sigaction(SIGCHLD, &action, NULL); }
    /* Launchers may inherit nice -15 from the camera key handler. nice +15 in
     * a shell is relative; set an absolute background priority for UI and jobs. */
    if (setpriority(PRIO_PROCESS, 0, 19) != 0)
        fprintf(stderr, "setpriority failed errno=%d\n", errno);
    /* 两层一次装齐：菜单页可以在进程内换层，只装一层的话切过去就是空列表。
     * LUT 每次 apply 都要写 19,712 B 槽再触发一次 DMA，逐格落地就是连续
     * 闪屏，所以停手 450 ms 再落一次；PW 预览只是标量直推，150 ms 跟手。 */
    for (i = 0; i < NLAYER; i++) {
        nx_queue_init(&layers[i].q);
        layers[i].q.debounce_ms = i == L_LUT ? NXUI_LUT_DEBOUNCE_MS : NXUI_DEBOUNCE_MS;
        layers[i].eng = i == L_LUT ? lut_engine : pw_engine;
        layers[i].path = i == L_LUT ? lut_recipes : pw_recipes;
        layers[i].n = nx_load_recipes(layers[i].path, layers[i].rec, NXUI_MAX_RECIPES);
        if (layers[i].n < 0) layers[i].n = 0;
        layers[i].sel = 0;
        fprintf(stderr, "LOAD %s entries=%d path=%s\n", i == L_LUT ? "lut" : "pw",
                layers[i].n, layers[i].path);
    }
    cur = &layers[start_layer & 1];
    /* LUT 层的两个输入域：启动永远停在 std，vlog 清单在第一次点 LUT 页签时
     * 才读 —— 面板首帧实测 355 ms，每一毫秒都算，不预读第二份清单。 */
    lut_path_std = lut_recipes;
    lut_path_vlog = lut_recipes_vlog;
    layers[L_LUT].path = lut_path_std;
    lut_scope = 0;
    /* 截图与自动化用：直接从 vlog 域起（正式入口永远停在 std，用户点页签
     * 才换过去）。没有这个口子就只能靠点击驱动，截不到 vlog 域那张图。 */
    if (getenv("NXUI_LUT_SCOPE") && !strcmp(getenv("NXUI_LUT_SCOPE"), "vlog")) {
        int vn;
        lut_scope = 1;
        layers[L_LUT].path = lut_path_vlog;
        vn = nx_load_recipes(lut_path_vlog, layers[L_LUT].rec, NXUI_MAX_RECIPES);
        layers[L_LUT].n = vn > 0 ? vn : 0;
        fprintf(stderr, "LOAD lut-vlog entries=%d path=%s\n", layers[L_LUT].n, lut_path_vlog);
    }
    if (getenv("NXUI_STATE_DIR")) state_dir = getenv("NXUI_STATE_DIR");
    /* 回到上次停的那一项；没有记录就留在第一项 */
    last_choice_load(L_PW);
    last_choice_load(L_LUT);
    keymap_defaults();
    keymap_load();        /* keys.conf 覆盖默认值；没有这个文件就是默认 */
    /* 无条件打一行：相机那份 libX11 的 keysym 名字表比桌面小，XF86* 若解析
     * 不出来会变成 "KEYMAP unknown keysym"，键位表就静默少几项。verify_display
     * 断言这个数等于 keymap_defaults 的条目数（当前 24），且没有 unknown；
     * 这份断言只有跑在相机 ABI（qemu + rootfs-112 的 libX11）上才算数。 */
    fprintf(stderr, "KEYMAP entries=%d\n", keymap_n);
    if (inputlog_path && *inputlog_path && (inputlog = fopen(inputlog_path, "a")))
        fprintf(inputlog, "=== nxui inputlog %s ===\n", lut_mode ? "lut" : "filmlab");
    recipes_ready = now_ms();
    display = XOpenDisplay(NULL);
    if (!display) { fprintf(stderr, "XOpenDisplay failed\n"); return 4; }
    display_ready = now_ms();
    screen = DefaultScreen(display); width = 720; height = 208;
    if (DisplayWidth(display, screen) < width || DisplayHeight(display, screen) < height) {
        fprintf(stderr, "Display must be at least 720x208\n"); XCloseDisplay(display); return 4;
    }
    memset(&attr, 0, sizeof attr);
    attr.override_redirect = True; attr.background_pixel = BlackPixel(display, screen);
    /* 面板现在有可拖动的滚动条：必须订阅按住拖动产生的 MotionNotify，
     * 否则滑块只响应按下、不跟随手指。触摸屏的滑动也会转成 motion。 */
    attr.event_mask = ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|Button1MotionMask|StructureNotifyMask;
    window = XCreateWindow(display, RootWindow(display, screen), 0, DisplayHeight(display, screen)-height,
        (unsigned)width, (unsigned)height, 0, DefaultDepth(display, screen), InputOutput,
        DefaultVisual(display, screen), CWOverrideRedirect|CWBackPixel|CWEventMask, &attr);
    XStoreName(display, window, "NXWA-Nuklear");
    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);
    if (font_explicit) {
        /* 调用方的明确选择，缺字与降级都由调用方负责。 */
        font = nk_xfont_create(display, font_name); font_tier = 2;
    } else if (!initial_menu && font_min && *font_min) {
        /* 一级只在显式设了 NXUI_FONT_MIN 时使用（实验档）。默认不用：实测
         * 按文件打开 38 KB 子集要 50 ms，按 family 打开 1.9 MB 子集只要
         * 24–53 ms，因为 FcFreeTypeQuery 的固定开销与 cmap 码点数几乎无关。
         * 保留这个入口是为了在其他固件/字体上重测时不必改代码。 */
        font = font_open_file(display, font_min, 19); font_tier = 0;
        if (!font || !font->ft) {
            fprintf(stderr, "FONT tier0 %s unavailable, trying %s\n", font_min, font_gb);
            if (font && font->ft) nk_xfont_del(display, font);
            font = nk_xfont_create(display, font_gb); font_tier = 1;
        }
    } else {
        font = nk_xfont_create(display, font_gb); font_tier = 1;
    }
    /* 末级兜底：按文件开原厂字体。生产 fonts.conf 刻意不含 /usr/share/fonts，
     * 所以这里不能退回家族名。 */
    if (!font || !font->ft) {
        if (font && font->ft) nk_xfont_del(display, font);
        font = font_open_file(display, font_file, 19); font_tier = 2;
    }
    if (!font || !font->ft) { fprintf(stderr, "font unavailable\n"); return 5; }
    font_ready = now_ms();
    ctx = nk_xlib_init(font, display, screen, window, DefaultVisual(display, screen),
        DefaultColormap(display, screen), (unsigned)width, (unsigned)height);
    if (!ctx) { fprintf(stderr, "Nuklear initialization failed\n"); return 5; }
    backend_ready = now_ms();
    /* XCopyArea otherwise emits NoExpose after every blit. Treating that as a
     * reason to redraw creates a self-sustaining busy loop on the camera. */
    XSetGraphicsExposures(display, xlib.surf->gc, False);
    ctx->style.window.padding = nk_vec2(12, 8); ctx->style.window.spacing = nk_vec2(8, 5);
    ctx->style.window.background = nk_rgb(24, 29, 33);
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(24, 29, 33));
    ctx->style.button.normal = nk_style_item_color(nk_rgb(45, 53, 59));
    ctx->style.button.hover = ctx->style.button.normal;
    ctx->style.button.active = nk_style_item_color(nk_rgb(88, 111, 79));
    ctx->style.button.text_normal = nk_rgb(234, 239, 231);
    ctx->style.button.rounding = 4;
    /* 滚动条不走 Nuklear 的 slider 皮肤：scrollbar_row() 自己画轨道与滑块，
     * 颜色直接写在调用处（轨道 45/53/59、已选段 88/111/79、滑块 203/233/153）。 */
    if (initial_menu && !load_menu(initial_menu)) return 6;
    /* 只有在用默认（子集）字体时才校验覆盖范围；--font 是调用方的明确选择，
     * 缺字由调用方自己负责。换字体必须走 backend 的接口，否则 ctx 里那份
     * nk_user_font 仍指向已经释放的 XFont。 */
    if (!font_explicit && font_tier < 2 && !font_covers_panel(font)) {
        XFont *alt = NULL;
        int covered = 0, miss0 = font_missing_cp;
        if (font_tier == 0) {
            /* 二级：GB2312 子集（1.9 MB / 7.5k 码点，实测 53 ms）。简体日常
             * 文本、原厂菜单项、中文 look 名都落在这档。 */
            alt = nk_xfont_create(display, font_gb);
            if (alt && alt->ft) {
                covered = font_covers_panel(alt);
                if (covered) {
                    nk_xlib_set_font(alt);
                    if (font->ft) nk_xfont_del(display, font);
                    font = alt; font_tier = 1;
                    if (miss0 > 0) fprintf(stderr, "FONT tier1=%s (tier0 missing U+%04X)\n", font_gb, miss0);
                    else fprintf(stderr, "FONT tier1=%s (tier0 unusable)\n", font_gb);
                } else {
                    if (alt->ft) nk_xfont_del(display, alt); else free(alt);
                }
            } else {
                if (!alt || !alt->ft) fprintf(stderr, "FONT tier1 %s unavailable\n", font_gb);
                if (alt) { if (alt->ft) nk_xfont_del(display, alt); else free(alt); }
            }
            alt = NULL;
        }
        if (!covered) {
            /* 三级：原厂 10.7 MB 字体（370 ms），按文件开——生产 fonts.conf
             * 刻意不含 /usr/share/fonts，退回家族名是找不到的。 */
            alt = font_open_file(display, font_file, 19);
            if (alt && alt->ft) {
                nk_xlib_set_font(alt);
                if (font->ft) nk_xfont_del(display, font);
                font = alt; font_tier = 2;
                if (font_missing_cp > 0) fprintf(stderr, "FONT tier2=%s (tier1 missing U+%04X)\n", font_file, font_missing_cp);
                else fprintf(stderr, "FONT tier2=%s (tier1 unusable)\n", font_file);
            } else if (alt) {
                free(alt);   /* ft 为 NULL 时不能交给 XftFontClose */
                fprintf(stderr, "FONT tier2 unavailable, keeping lower tier\n");
            }
        }
    }
    font_final = now_ms();   /* 字体链最终确定（含所有降级），INIT 的 font_total_ms 用 */
    save_focus();   /* remember the camera window that owns the keyboard now */
    XMapRaised(display, window); XSync(display, False);
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    started = now_ms();
    fprintf(stderr, "READY backend=nuklear-xft live=%d recipes=%d panel=%dx%d nice=%d tier=%d sel=%d\n",
        live, count, width, height, getpriority(PRIO_PROCESS, 0), font_tier, selected);
    /* font_ms 只覆盖"打开第一份字体"；font_total_ms 是从进程启动到字体链
     * 最终确定的累计时间（含校验与降级）。后者减掉 recipes+display 就是
     * 字体环节的真实成本。 */
    fprintf(stderr, "INIT recipes_ms=%lld display_ms=%lld font_ms=%lld backend_ms=%lld map_ms=%lld font_total_ms=%lld\n",
        recipes_ready-process_started, display_ready-recipes_ready, font_ready-display_ready,
        backend_ready-font_ready, started-backend_ready, font_final-process_started);
    if (live) { ev_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK); }
    if (live) start_child(6, -1, "st cap capdtm getusr 20");
    /* --bench-frames N: steady-state cost of one frame, without the frame
     * pacing. The trailing XSync makes the number include the server actually
     * consuming the requests, not just Xlib filling its output buffer. Run it
     * on the host for A/B builds, or on the body for a real budget. */
    if (bench_frames > 0) {
        int n; long long bench_started = now_ms();
        XSync(display, False);
        for (n = 0; n < bench_frames; n++) {
            long long frame_started = now_ms();
            draw(); render_count++;
            draw_total_ms += now_ms() - frame_started; draw_calls++;
        }
        XSync(display, False);
        fprintf(stderr, "BENCH frames=%d total_ms=%lld avg_ms=%.2f\n", bench_frames,
            now_ms()-bench_started, (double)(now_ms()-bench_started)/bench_frames);
        quit = 1;
    }
    while (!quit) {
        XEvent event; fd_set fds; struct timeval wait; int pending, events = 0, refresh = 0, painted = 0;
        long long tick = now_ms(), delay = 60000;
        if (interrupted || (lifetime > 0 && now_ms()-started >= lifetime)) queue.close_requested = 1;
        /* 半按快门 S1：/dev/input/event0 code 125 (KEY_LEFTMETA) press ⇒ 关闭 */
        if (ev_fd >= 0) {
            struct input_event iev;
            while (read(ev_fd, &iev, sizeof iev) == (ssize_t)sizeof iev) {
                if (iev.type == EV_KEY && iev.code == 125 && iev.value == 1) {
                    /* A half-press is the operator's "close and shoot" gesture.
                     * Nothing is restored on the way out -- neither here nor on
                     * the EV_S1.sh (/tmp/nxui-close) path, which used to undo
                     * the look the half-press itself was supposed to load. */
                    queue.close_requested = 1;
                }
            }
        }
        /* EV_S1.sh 通道（备用）：每 100ms 检查并消费标记 */
        if (tick - close_flag_check >= 100) {
            close_flag_check = tick;
            if (access("/tmp/nxui-close", F_OK) == 0) {
                unlink("/tmp/nxui-close");
                queue.close_requested = 1;
            }
        }
        /* Leave unconsumed input in Xlib until its frame. Resetting Nuklear's
         * input while deferring draw would discard fast taps. Bound each batch
         * and render to 10 Hz, including an expose/key-repeat event storm. */
        if (tick >= next_frame) {
          nk_input_begin(ctx);
          while (events < 64 && XPending(display)) {
            XNextEvent(display, &event);
            events++;
            if (event.type == NoExpose) continue;
            if (event.type == MotionNotify) {
                /* 拖动滚动条：更新指针位置；只有左键按下（拖动中）才重绘。
                 * 判断用 Nuklear 自己的按键状态 —— 合成 motion（XSendEvent）
                 * 的 xmotion.state 常被服务器清零，用它会导致整段拖动不跟手。
                 * 纯 hover 不置脏：相机触摸板会持续发 motion，全量重绘会让
                 * 空闲面板变成 10Hz 常亮渲染。 */
                if (press_active &&
                    (abs(event.xmotion.x - press_x) > 12 || abs(event.xmotion.y - press_y) > 12))
                    press_active = 0;   /* 手指在动 ⇒ 这是滑动，不是长按 */
                nk_input_motion(ctx, event.xmotion.x, event.xmotion.y);
                if (ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) dirty = 1;
                continue;
            }
            if (event.type == Expose) { refresh = 1; continue; }
            if (event.type == ConfigureNotify) {
                /* The event already contains the size. Avoid the upstream
                 * synchronous XGetWindowAttributes on every expose/resize. */
                if (width != event.xconfigure.width || height != event.xconfigure.height) {
                    width = event.xconfigure.width; height = event.xconfigure.height;
                    nk_xsurf_resize(xlib.surf, (unsigned)width, (unsigned)height);
                    dirty = 1;
                }
                continue;
            }
            if (event.type == KeyPress || event.type == ButtonPress || event.type == ButtonRelease) dirty = 1;
            if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == wm_delete) queue.close_requested = 1;
            if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                /* 键位表驱动（nxui/keys.conf 可覆盖）。默认仍是方向键/回车
                 * /Esc —— 与旧版逐键等价，但机身转盘的实测 keysym 只要写进
                 * 配置就能用，不必改二进制。 */
                if (inputlog) {
                    char b[160];
                    snprintf(b, sizeof b, "KEY sym=%s code=%u state=0x%x",
                             XKeysymToString(key) ? XKeysymToString(key) : "?",
                             event.xkey.keycode, event.xkey.state);
                    logline(b);
                }
                do_action(keymap_action(key, event.xkey.keycode));
            }
            /* 采数时按钮事件也要记：机身转盘有可能以 X 的滚轮形式（button 4/5）
             * 到达，只记 KeyPress 会把它整条漏掉。触摸的 tap 也会落这里，但
             * 采数的前提是「别碰屏幕」，多出来的 BTN 行本身就是信息。
             * MotionNotify 刻意不记 —— 手指一拖就是几百行。 */
            if (inputlog && (event.type == ButtonPress || event.type == ButtonRelease)) {
                char b[192];
                snprintf(b, sizeof b, "%s button=%u x=%d y=%d",
                         event.type == ButtonPress ? "BTN" : "BTNUP",
                         event.xbutton.button, event.xbutton.x, event.xbutton.y);
                logline(b);
            }
            /* 对话框（多行提示）命中：只有「关闭」那一块收它，别处的点击
             * 也不该穿到面板下面的按钮上 —— 挡住了就当模态。 */
            if (dialog_on && event.type == ButtonPress) {
                float mx = (float)event.xbutton.x, my = (float)event.xbutton.y;
                if (mx >= dialog_close.x && mx <= dialog_close.x + dialog_close.w &&
                    my >= dialog_close.y && my <= dialog_close.y + dialog_close.h) {
                    dialog_on = 0; dirty = 1;
                }
                continue;
            }
            if (event.type == ButtonPress && !menu && (event.xbutton.button == 4 || event.xbutton.button == 5))
                choose(selected + (event.xbutton.button == 4 ? -1 : 1));
            /* Touchscreens can send a tap without a preceding MotionNotify. */
            if (event.type == ButtonPress || event.type == ButtonRelease)
                nk_input_motion(ctx, event.xbutton.x, event.xbutton.y);
            /* 长按计时：按下记位置与时刻，motion 超过阈值就取消（见上）；
             * 到 550ms 还没动 ⇒ 进编辑态。触摸屏与鼠标走同一条路。 */
            if (event.type == ButtonPress && event.xbutton.button == 1 && !menu) {
                press_active = 1; press_x = event.xbutton.x; press_y = event.xbutton.y;
                press_at = now_ms();
                if (inputlog) {
                    char b[96];
                    snprintf(b, sizeof b, "PRESS x=%d y=%d", press_x, press_y);
                    logline(b);
                }
            }
            if (event.type == ButtonRelease) {
                press_active = 0;
                if (inputlog) {
                    char b[96];
                    snprintf(b, sizeof b, "RELEASE x=%d y=%d", event.xbutton.x, event.xbutton.y);
                    logline(b);
                }
            }
            /* ★ 2026-11-02 删掉左右滑动手势（原为「滑得远切得快」）。
             * 理由：它和「选中即居中」的重排叠在一起，手指刚落下整排就平移，
             * 滑动再切一次会把目标甩掉，长按也稳不住。翻页现在只有三条路：
             * 滚条拖动（跨全库最快）、上下键 / EV 拨盘、右上拨盘。够用。 */
            nk_xlib_handle_event(display, screen, window, &event);
            /* Keep press/release transitions in separate frames, even when a
             * complete short tap was queued while the camera was busy. */
            if (event.type == ButtonPress || event.type == ButtonRelease) break;
          }
          nk_input_end(ctx);
        }
        poll_child();
        /* 库变更后的列表重建。LUT 的顺序与增删由 lut.sh 决定，面板不自己
         * 推断——重跑一次 list，把它的输出当唯一答案；配方表是
         * filmlab.sh export 的产物，直接重读即可。 */
        if (!child && refresh_pending) {
            int mode = refresh_pending;
            refresh_pending = 0;
            if (mode == 2) {
                int saved = (int)(cur - layers);
                cur = &layers[L_PW];
                if (reload_recipes_file()) message("配方库已刷新");
                else message("配方表重读失败 · 见日志");
                cur = &layers[saved];
            } else {
                char list_cmd[600];
                /* vlog 域跑的是同一支 lut.sh，带 --vlog 才列 _vlog 那本目录 */
                snprintf(list_cmd, sizeof list_cmd, "sh %s list%s", layers[L_LUT].eng,
                         lut_scope ? " --vlog" : "");
                if (!start_child(5, -1, list_cmd)) message("无法刷新库列表");
            }
        }
        /* 长按（550ms 不动）进编辑态。触摸屏和鼠标共用这一条：
         * motion 超过 12px 时 press_active 已在上面的分支里清掉。
         * ★ 2026-11-02 试过把长按改作「标记 A/B」，实机上一按就跳：列表是
         *   「选中项居中」的 5 项窗口（first = selected - win/2），press 选中
         *   的那一瞬间整排会平移，手指底下换成了另一条，长按根本对不准。
         *   所以长按还给编辑态，A/B 仍然在混合态里用按钮设。 */
        if (press_active && !child && now_ms() - press_at >= 550) {
            press_active = 0;
            if (ui_mode == UI_BROWSE) do_action(ACT_EDIT);
        }
        if (confirm_del && now_ms() - confirm_del_at > 3000) { confirm_del = 0; dirty = 1; }
        /* 横幅：4 Hz 取一次文件，别让它跟 10 Hz 重绘抢单核 */
        {
            long long now = now_ms();
            if (now - banner_check >= 250) {
                FILE *bf;
                banner_check = now;
                if ((bf = fopen("/tmp/nxui-banner", "r")) != NULL) {
                    char bl[320];
                    if (fgets(bl, sizeof bl, bf)) {
                        char *tab;
                        int ttl = 3;
                        bl[strcspn(bl, "\r\n")] = 0;
                        if ((tab = strchr(bl, '\t')) != NULL) { *tab = 0; ttl = atoi(tab + 1); }
                        if (ttl < 1) ttl = 1;
                        if (ttl > 20) ttl = 20;
                        snprintf(banner, sizeof banner, "%s", bl);
                        banner_until = now + (long long)ttl * 1000;
                        fprintf(stderr, "BANNER %s ttl=%d\n", banner, ttl);
                        dirty = 1;
                    }
                    fclose(bf);
                    unlink("/tmp/nxui-banner");
                }
                if (banner[0] && now > banner_until) { banner[0] = 0; dirty = 1; }
                /* 多行对话框：整份文件读进来，关掉之前一直显示 */
                if ((bf = fopen("/tmp/nxui-dialog", "r")) != NULL) {
                    size_t got = fread(dialog, 1, sizeof dialog - 1, bf);
                    dialog[got] = 0;
                    /* 去掉首尾空行：脚本用 heredoc 写，末尾常带换行 */
                    while (got && (dialog[got - 1] == '\n' || dialog[got - 1] == '\r')) dialog[--got] = 0;
                    if (got) { dialog_on = 1; dirty = 1; fprintf(stderr, "DIALOG %u bytes\n", (unsigned)got); }
                    fclose(bf);
                    unlink("/tmp/nxui-dialog");
                }
            }
        }
        refresh_services(0);
        /* Unmap as soon as a close is requested: while the backend finishes
         * (up to the 12 s timeout) the panel must not keep covering the frame
         * or hold the keyboard focus away from the camera UI. */
        if (queue.close_requested && !panel_unmapped) {
            restore_focus();
            XUnmapWindow(display, window);
            XSync(display, False);
            panel_unmapped = 1;
        }
        /* Closing waits for an in-flight write. In the LUT layer a selection
         * that is still inside its debounce window is not discarded either: the
         * operator's half-press means "this one", and the slot must end up
         * holding what the panel showed. At most one extra run -- nx_queue_select
         * overwrites, so only the last selection can be pending. FILMLAB keeps
         * the old drop-on-close behaviour: its preview is not a commitment.
         * ★ 两层各有一条队列：关面板时要看的是 LUT 那条，不管当前停在哪一层
         *   （在 LUT 选完就切到 PW、然后关，这次写入同样不能丢）。 */
        if (queue.close_requested && !queue.commit_requested && !child && !lut_unsettled()) break;
        /* 关闭是用户的明确确认，不必再等 LUT 那 450 ms 去抖窗口。 */
        if (queue.close_requested && lut_unsettled()) layers[L_LUT].q.due = 0;
        dispatch_pending();
        if (tick >= next_frame && !quit) {
            if (dirty) {
                long long draw_started = now_ms(), elapsed;
                dirty = 0; draw(); render_count++; painted = 1;
                if (render_count == 1) {
                    const char *launch = getenv("NXUI_LAUNCH_UPTIME");
                    long long launch_ms = launch ? (long long)(atof(launch)*1000) : process_started;
                    XSync(display, False); /* one initial fence measures visible first frame */
                    fprintf(stderr, "FIRST_FRAME process_ms=%lld launch_ms=%lld\n",
                        now_ms()-process_started, now_ms()-launch_ms);
                }
                elapsed = now_ms() - draw_started;
                draw_total_ms += elapsed; draw_calls++;
                if (elapsed > max_draw_ms) max_draw_ms = elapsed;
            } else if (refresh && render_count) {
                /* Retained pixmap: expose needs one copy, no layout/font work. */
                XSetClipMask(display, xlib.surf->gc, None);
                nk_xsurf_blit(window, xlib.surf, (unsigned)width, (unsigned)height);
                XFlush(display); blits++;
            }
            if (events || painted || refresh) next_frame = now_ms() + frame_ms;
        }
        if (shot && render_count) { snapshot(shot); break; }
        tick = now_ms(); pending = XPending(display);
        /* 后端在跑时只做回收，不必每秒醒 10 次。原来写死 100ms，等于给每次
         * 预览白加最多 100ms 的「完成感知」延迟 —— 实测预览 930ms 里有 ~100 是
         * 这一行。18ms 约等于一帧，够省 CPU，也够了。 */
        if (child) delay = 18; /* only a running backend needs periodic reaping */
        if (services_visible() && service_due - tick < delay) delay = service_due - tick;
        /* 两层各自的到期时刻都要参与 select 超时：不在当前层的那个请求
         * 同样是用户的意图，不能被 60 s 的空闲睡眠吞掉。 */
        for (int L = 0; L < NLAYER; L++) {
            struct nx_queue *q = &layers[L].q;
            if (!child && q->desired >= 0 && (q->desired != q->applied || q->commit_requested) &&
                q->due - tick < delay)
                delay = q->due - tick;
        }
        if (lifetime > 0 && !queue.close_requested && started + lifetime - tick < delay)
            delay = started + lifetime - tick;
        if ((dirty || pending) && next_frame - tick < delay) delay = next_frame - tick;
        /* Live panel without evdev: no interrupt carries a half-press, so the
         * flag file is the only channel left. Poll it at 4 Hz rather than let
         * the 60 s idle timeout swallow the gesture. Dry runs have no gesture
         * to catch and keep the long sleep. */
        if (live && ev_fd < 0 && !child && delay > 250) delay = 250;
        /* 横幅文件是外部写进来的（nxpopup.sh），面板睡着时不看它就等于没有。
         * 400 ms 够用：脚本写文件到横幅出现最多慢 0.4 s，比 60 s 的空闲睡眠好得多。 */
        if (live && delay > 400) delay = 400;
        /* 长按与二次确认都是"到点做事"，不能等到 60 s 的空闲超时才发现：
         * 按下之后没有新事件时，select 会睡满上一个 delay。 */
        if (press_active) {
            long long left = press_at + 550 - now_ms();
            if (left < delay) delay = left < 0 ? 0 : left;
        }
        if (confirm_del) {
            long long left = confirm_del_at + 3000 - now_ms();
            if (left < delay) delay = left < 0 ? 0 : left;
        }
        /* 横幅到期要准时消失，不能被 60 s 的空闲睡眠拖住 */
        if (banner[0]) {
            long long left = banner_until - now_ms();
            if (left < delay) delay = left < 0 ? 0 : left;
        }
        if (delay < 0) delay = 0;
        FD_ZERO(&fds);
        /* Pending events are already in the client queue. Watching a readable
         * server fd before the frame deadline would spin under an input burst. */
        if (!pending) FD_SET(ConnectionNumber(display), &fds);
        if (child_fd >= 0 && !pipe_eof) FD_SET(child_fd, &fds);
        wait.tv_sec = (long)(delay/1000); wait.tv_usec = (long)(delay%1000)*1000;
        select((child_fd > ConnectionNumber(display) ? child_fd : ConnectionNumber(display))+1, &fds, NULL, NULL, &wait);
        wakeups++;
    }
    /* 只在真机上记位置：--snapshot 是测试路径，不该改写用户的卡片。
     * 测试环境没有 /mnt/mmc/nxui，fopen 失败即静默跳过，天然隔离。 */
    if (!shot) { last_choice_save(L_PW); last_choice_save(L_LUT); }
    fprintf(stderr, "EXIT renders=%d elapsed_ms=%lld wakeups=%d blits=%d max_draw_ms=%lld draw_avg_ms=%.2f draw_n=%d\n",
        render_count, now_ms()-started, wakeups, blits, max_draw_ms,
        draw_calls ? (double)draw_total_ms/draw_calls : 0.0, draw_calls);
    restore_focus();   /* also covers --snapshot / lifetime / signal exits */
    nk_xlib_shutdown(); nk_xfont_del(display, font);
    /* No restore on exit. Every LUT write lands in RAM and a power cycle
     * reloads the image copy, so leaving the panel cannot damage anything --
     * while auto-restoring here silently undid the look on "save and close",
     * on OK, and on every half-press routed through EV_S1.sh.
     * "还原原厂" in the panel is the only path that calls lut.sh off. */
    XDestroyWindow(display, window); XCloseDisplay(display); close(lockfd);
    return 0;
}
