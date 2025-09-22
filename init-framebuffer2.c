#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

int main() {
    int fb = open("/dev/fb0", 2);
    uint32_t *p = mmap(0, 1920*1080*4, PROT_READ|PROT_WRITE, MAP_SHARED, fb, 0);
    for (long i = 0; i < 1920*1080; i++) p[i] = 0x00FF0000;
    pause();
}
