// build: gcc -static -O2 -Wall -Wextra -s -o myinit init-framebuffer4.c
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

static int con_fd = -1, kmsg_fd = -1;

static void open_logs(void) {
    if (access("/dev/console", W_OK) != 0) {
        mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    }
    con_fd = open("/dev/console", O_WRONLY | O_NOCTTY);
    kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
}

__attribute__((format(printf,1,2)))
static void klog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (con_fd >= 0) vdprintf(con_fd, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    if (kmsg_fd >= 0) vdprintf(kmsg_fd, fmt, ap);
    va_end(ap);
}

__attribute__((noreturn, format(printf,1,2)))
static void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (con_fd >= 0) {
        dprintf(con_fd, "init[FATAL]: ");
        vdprintf(con_fd, fmt, ap);
        dprintf(con_fd, " (errno=%d)\n", errno);
    }
    va_end(ap);
    va_start(ap, fmt);
    if (kmsg_fd >= 0) {
        dprintf(kmsg_fd, "init[FATAL]: ");
        vdprintf(kmsg_fd, fmt, ap);
        dprintf(kmsg_fd, " (errno=%d)\n", errno);
    }
    va_end(ap);

    raise(SIGSEGV);        // force panic so you get a dump
    for (;;) pause();
}

int main(void) {
    open_logs();
    klog("init: starting (PID 1)\n");

    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) fatal("open /dev/fb0 failed: %m");

    struct fb_var_screeninfo v;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &v) < 0)
        fatal("FBIOGET_VSCREENINFO failed: %m");

    struct fb_fix_screeninfo f;
    if (ioctl(fb, FBIOGET_FSCREENINFO, &f) < 0)
        fatal("FBIOGET_FSCREENINFO failed: %m");

    if (v.bits_per_pixel != 32)
        fatal("unsupported bpp: %u (need 32)", v.bits_per_pixel);

    size_t len = f.smem_len;
    uint8_t *base = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (base == MAP_FAILED) fatal("mmap fb failed: %m");

    volatile uint32_t *pix = (uint32_t *)(base + 0); // (0,0)
    *pix = 0x00FF0000; // red (XRGB8888)

    klog("init: wrote red pixel at (0,0); %ux%u@%u, line=%u, mem=%u\n",
         v.xres, v.yres, v.bits_per_pixel, f.line_length, f.smem_len);

    signal(SIGINT, SIG_IGN);
    for (;;) pause();
}
