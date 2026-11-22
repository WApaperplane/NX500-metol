/* fakeclick —— 用 XTEST 在相机 X server 上合成一次「移动 + 按下 + 抬起」。
 *
 * 用途：把「nxui 收不到触摸」拆成两半。
 *   1) 合成点击也不响应  -> 问题在 nxui 自己（事件循环/命中），排查代码
 *   2) 合成点击有响应    -> nxui 代码没问题，真实触摸根本没变成 core 事件
 *
 * XTEST 事件由 X server 内部生成，与真实输入的投递路径一致（命中测试照做），
 * 但不经过触摸驱动 —— 正好用来隔离设备层。
 *
 * 相机 rootfs 没带 XTest.h，原型手写；链接直接用相机自己的 libXtst.so.6。
 * 编译见 tools/build-fakeclick.sh。
 */
#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* -std=c99 下 <unistd.h> 不暴露 usleep；用 nanosleep 自备一个。 */
static void delay_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

extern int XTestQueryExtension(Display *dpy, int *ev, int *err, int *maj, int *min);
extern int XTestFakeMotionEvent(Display *dpy, int screen, int x, int y, unsigned long delay);
extern int XTestFakeButtonEvent(Display *dpy, unsigned int button, int is_press, unsigned long delay);

int main(int argc, char **argv)
{
    Display *dpy;
    int x = 656, y = 164, hold_ms = 120, i, n = 1;

    if (argc >= 3) { x = atoi(argv[1]); y = atoi(argv[2]); }
    if (argc >= 4) n = atoi(argv[3]);
    if (argc >= 5) hold_ms = atoi(argv[4]);

    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "fakeclick: XOpenDisplay failed\n"); return 1; }

    {
        int ev, err, maj, min;
        if (!XTestQueryExtension(dpy, &ev, &err, &maj, &min)) {
            fprintf(stderr, "fakeclick: XTEST not available\n"); return 2;
        }
    }

    for (i = 0; i < n; i++) {
        XTestFakeMotionEvent(dpy, -1, x, y, 0);
        XSync(dpy, False);
        delay_ms(150);
        XTestFakeButtonEvent(dpy, 1, True, 0);
        XSync(dpy, False);
        delay_ms(hold_ms);
        XTestFakeButtonEvent(dpy, 1, False, 0);
        XSync(dpy, False);
        delay_ms(250);
    }
    fprintf(stderr, "fakeclick: %d click(s) at %d,%d hold=%dms\n", n, x, y, hold_ms);
    XCloseDisplay(dpy);
    return 0;
}
