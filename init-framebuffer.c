#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

int main() {
    int fd = open("/dev/fb0", 2);
    if (fd < 0) return 1;
    
    struct fb_var_screeninfo vinfo;
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    
    int screensize = vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8;
    char *fbp = mmap(0, screensize, 3, 1, fd, 0);
    
    // Clear screen (black)
    for (int i = 0; i < screensize; i += 4) {
        *(int*)(fbp + i) = 0x00000000;
    }
    
    int cx = vinfo.xres / 2, cy = vinfo.yres / 2;
    int bpp = vinfo.bits_per_pixel / 8;
    
    // Draw smiley face
    for (int y = -50; y <= 50; y++) {
        for (int x = -50; x <= 50; x++) {
            int px = cx + x, py = cy + y;
            if (px >= 0 && px < vinfo.xres && py >= 0 && py < vinfo.yres) {
                int pos = (py * vinfo.xres + px) * bpp;
                // Face circle
                if (x*x + y*y <= 2500) *(int*)(fbp + pos) = 0x00FFFF00;
                // Eyes
                if ((x+15)*(x+15) + (y-15)*(y-15) <= 25 || (x-15)*(x-15) + (y-15)*(y-15) <= 25)
                    *(int*)(fbp + pos) = 0x00000000;
                // Mouth
                if (y > 10 && y < 25 && x*x + (y-35)*(y-35) <= 625 && x*x + (y-20)*(y-20) >= 225)
                    *(int*)(fbp + pos) = 0x00000000;
            }
        }
    }
    
    for (;;) pause();
}
