// build: gcc -static -O2 -Wall -s -o myinit init-min-drm.c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

static int con = -1;

static void logf(const char *fmt, ...) {
    if (con < 0) return;
    va_list ap; va_start(ap, fmt); vdprintf(con, fmt, ap); va_end(ap);
}
__attribute__((noreturn))
static void die(const char *msg) {
    if (con >= 0) dprintf(con, "init[FATAL]: %s (errno=%d)\n", msg, errno);
    raise(SIGSEGV); for (;;) pause();
}

int main(void) {
    // minimal mounts so device nodes and modules work
    mkdir("/proc", 0555);  mount("proc", "/proc", "proc", 0, 0);
    mkdir("/sys", 0555);   mount("sysfs", "/sys", "sysfs", 0, 0);
    mkdir("/dev", 0755);   mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");

    // console for messages
    if (access("/dev/console", W_OK) != 0) mknod("/dev/console", S_IFCHR|0600, makedev(5,1));
    con = open("/dev/console", O_WRONLY|O_NOCTTY);

    // load VirtualBox KMS driver (VMSVGA) – keep it tiny; rely on /bin/sh
    // replace your modprobe call(s) with:
    (void)system("/sbin/modprobe -q vboxvideo || /usr/sbin/modprobe -q vboxvideo || true");
    (void)system("/sbin/modprobe -q drm_kms_helper || true");   // fbdev emu, harmless
    // optional fallbacks if you switch hypervisor later:
    (void)system("/sbin/modprobe -q virtio_gpu || true");
    (void)system("/sbin/modprobe -q simpledrm || true");

    // try to open DRM card a few times
    int drm = -1;
    for (int i = 0; i < 20; i++) {
        drm = open("/dev/dri/card0", O_RDWR|O_CLOEXEC);
        if (drm >= 0) break;
        usleep(100000);
    }
    if (drm < 0) die("open /dev/dri/card0 failed");

    DIR *d = opendir("/sys/class/drm");
    if (d) { struct dirent *e; while ((e = readdir(d))) dprintf(con, "drm: %s\n", e->d_name); closedir(d); }

    // query resources (sizes, then fill)
    struct drm_mode_card_res res = {0};
    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) die("GETRESOURCES sizes");
    uint32_t *con_ids = res.count_connectors ? malloc(res.count_connectors*4) : NULL;
    uint32_t *enc_ids = res.count_encoders   ? malloc(res.count_encoders*4)   : NULL;
    uint32_t *crtc_ids= res.count_crtcs      ? malloc(res.count_crtcs*4)      : NULL;
    res.connector_id_ptr = (uintptr_t)con_ids;
    res.encoder_id_ptr   = (uintptr_t)enc_ids;
    res.crtc_id_ptr      = (uintptr_t)crtc_ids;
    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) die("GETRESOURCES fill");

    // pick a connected connector + mode
    uint32_t connector_id = 0, encoder_id = 0;
    struct drm_mode_modeinfo mode = {0};
    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector c = { .connector_id = con_ids[i] };
        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) continue;
        struct drm_mode_modeinfo *modes = c.count_modes ? malloc(c.count_modes*sizeof(*modes)) : NULL;
        uint32_t *cencs = c.count_encoders ? malloc(c.count_encoders*4) : NULL;
        c.modes_ptr = (uintptr_t)modes; c.encoders_ptr = (uintptr_t)cencs;
        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) { free(modes); free(cencs); continue; }
        if (c.connection == DRM_MODE_CONNECTED && c.count_modes) {
            connector_id = c.connector_id; mode = modes[0];
            encoder_id = c.encoder_id ? c.encoder_id : (c.count_encoders ? cencs[0] : 0);
            free(modes); free(cencs); break;
        }
        free(modes); free(cencs);
    }
    if (!connector_id) die("no connected connector");

    // get encoder → pick CRTC
    struct drm_mode_get_encoder enc = { .encoder_id = encoder_id };
    if (ioctl(drm, DRM_IOCTL_MODE_GETENCODER, &enc) < 0) die("GETENCODER");
    uint32_t crtc_id = enc.crtc_id ? enc.crtc_id : crtc_ids[0];
    for (uint32_t i = 0; !enc.crtc_id && i < res.count_crtcs; i++)
        if (enc.possible_crtcs & (1u<<i)) { crtc_id = crtc_ids[i]; break; }

    // dumb buffer
    struct drm_mode_create_dumb creq = { .width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32 };
    if (ioctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) die("CREATE_DUMB");

    struct drm_mode_fb_cmd2 fb = {0};
    fb.width = creq.width; fb.height = creq.height;
    fb.pixel_format = DRM_FORMAT_XRGB8888;
    fb.pitches[0] = creq.pitch; fb.handles[0] = creq.handle;
    if (ioctl(drm, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) die("ADDFB2");

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (ioctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) die("MAP_DUMB");
    uint8_t *map = mmap(NULL, creq.size, PROT_READ|PROT_WRITE, MAP_SHARED, drm, mreq.offset);
    if (map == MAP_FAILED) die("mmap dumb");

    // draw: black bg, yellow face, eyes, smile
    const uint32_t BLACK=0x00000000, YELLOW=0x00FFFF00, BLACKPX=0x00000000;
    for (uint32_t y=0; y<creq.height; y++) {
        uint32_t *row=(uint32_t*)(map + y*creq.pitch);
        for (uint32_t x=0; x<creq.width; x++) row[x]=BLACK;
    }
    int cx=creq.width/2, cy=creq.height/2, r=(creq.height<creq.width?creq.height:creq.width)/6;
    for (int y=-r; y<=r; y++) {
        uint32_t *row=(uint32_t*)(map + (cy+y)*creq.pitch);
        for (int x=-r; x<=r; x++) {
            int px=cx+x, py=cy+y; if ((unsigned)px>=creq.width || (unsigned)py>=creq.height) continue;
            int d2=x*x+y*y; if (d2<=r*r) row[px]=YELLOW;
            int ex=r/2, ey=-r/3, er=r/10;
            if (((x+ex)*(x+ex)+(y+ey)*(y+ey)<=er*er) || ((x-ex)*(x-ex)+(y+ey)*(y+ey)<=er*er)) row[px]=BLACKPX;
            if (y>0) { int mr=r/2; int d2i=x*x+(y-mr/3)*(y-mr/3);
                if (d2i>=(mr-2)*(mr-2)&&d2i<=(mr+2)*(mr+2)) row[px]=BLACKPX; }
        }
    }

    // display
    struct drm_mode_crtc crtc = {0};
    crtc.crtc_id = crtc_id; crtc.fb_id = fb.fb_id;
    crtc.set_connectors_ptr=(uintptr_t)&connector_id; crtc.count_connectors=1;
    crtc.mode = mode; crtc.mode_valid=1;
    if (ioctl(drm, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) die("SETCRTC");

    dprintf(con, "init: smiley at %ux%u (pitch=%u)\n", creq.width, creq.height, creq.pitch);
    signal(SIGINT, SIG_IGN);
    for (;;) pause();
}
