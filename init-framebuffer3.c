#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <stdint.h>

int main() {
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) for (;;) pause();   // stay alive if fb0 missing

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) for (;;) pause();

    long screensize = (long)vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    uint8_t *p = mmap(0, screensize, PROT_READ|PROT_WRITE, MAP_SHARED, fb, 0);
    if (p == MAP_FAILED) for (;;) pause();

    // Only handle 32 bpp safely
    if (vinfo.bits_per_pixel == 32) {
        uint32_t *pix = (uint32_t *)p;
        pix[0] = 0x00FF0000; // red in XRGB8888
    }

    for (;;) pause(); // never exit as PID 1
}
