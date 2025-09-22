// gcc -static -O2 -Wall -Wextra -s -o myinit init-drm.c
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#ifndef DRM_MODE_DISCONNECTED
#define DRM_MODE_DISCONNECTED 2
#endif
#include <drm/drm_fourcc.h>

static int con_fd = -1;

static void open_console(void) {
    if (access("/dev/console", W_OK) != 0) {
        mknod("/dev/console", S_IFCHR | 0600, makedev(5,1));
    }
    con_fd = open("/dev/console", O_WRONLY | O_NOCTTY);
}

__attribute__((format(printf,1,2)))
static void logc(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (con_fd >= 0) vdprintf(con_fd, fmt, ap);
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
    raise(SIGSEGV);
    for (;;) pause();
}

static void mount_basic(void) {
    mkdir("/proc", 0555);  mount("proc", "/proc", "proc", 0, 0);
    mkdir("/sys", 0555);   mount("sysfs", "/sys", "sysfs", 0, 0);
    mkdir("/dev", 0755);   mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755");
}

static uint32_t pick_crtc_id(const struct drm_mode_card_res *res,
                             const struct drm_mode_get_encoder *enc) {
    // Access arrays through the *_ptr fields
    const uint32_t *crtc_ids = (const uint32_t *)(uintptr_t)res->crtc_id_ptr;

    if (enc->crtc_id) return enc->crtc_id;

    for (uint32_t i = 0; i < res->count_crtcs; i++) {
        if (enc->possible_crtcs & (1u << i)) return crtc_ids[i];
    }
    // Fallback: first CRTC
    return crtc_ids[0];
}

int main(void) {
     system("/sbin/modprobe -q simpledrm");
    system("/sbin/modprobe -q vboxvideo");
    system("/sbin/modprobe -q drm_kms_helper");
    system("/sbin/modprobe -q virtio_gpu");
    system("/sbin/modprobe -q bochs-drm");
    system("/sbin/modprobe -q i915");
    system("/sbin/modprobe -q amdgpu");
    system("/sbin/modprobe -q nouveau");
    
    mount_basic();
    open_console();
    logc("init: DRM dumb-buffer demo starting (PID 1)\n");

    int drm = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm < 0) fatal("open /dev/dri/card0 failed: %m");

    // 1) Query resources (sizes)
    struct drm_mode_card_res res = {0};
    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        fatal("GETRESOURCES(sizes) failed: %m");

    // allocate arrays
    uint32_t *con_ids = res.count_connectors ? malloc(res.count_connectors * sizeof(uint32_t)) : NULL;
    uint32_t *enc_ids = res.count_encoders   ? malloc(res.count_encoders   * sizeof(uint32_t)) : NULL;
    uint32_t *crtc_ids= res.count_crtcs      ? malloc(res.count_crtcs      * sizeof(uint32_t)) : NULL;
    uint32_t *fb_ids  = res.count_fbs        ? malloc(res.count_fbs        * sizeof(uint32_t)) : NULL;
    if ((res.count_connectors && !con_ids) || (res.count_encoders && !enc_ids) ||
        (res.count_crtcs && !crtc_ids) || (res.count_fbs && !fb_ids))
        fatal("malloc failed");

    res.connector_id_ptr = (uintptr_t)con_ids;
    res.encoder_id_ptr   = (uintptr_t)enc_ids;
    res.crtc_id_ptr      = (uintptr_t)crtc_ids;
    res.fb_id_ptr        = (uintptr_t)fb_ids;

    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        fatal("GETRESOURCES(fill) failed: %m");

    // 2) Pick a connected connector + mode
    uint32_t connector_id = 0;
    struct drm_mode_modeinfo mode = {0};
    uint32_t encoder_id = 0;

    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector conn = {0};
        conn.connector_id = con_ids[i];

        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

        struct drm_mode_modeinfo *modes = NULL;
        uint32_t *conn_encs = NULL;
        if (conn.count_modes)  modes = malloc(conn.count_modes * sizeof(*modes));
        if (conn.count_encoders) conn_encs = malloc(conn.count_encoders * sizeof(*conn_encs));
        conn.modes_ptr    = (uintptr_t)modes;
        conn.encoders_ptr = (uintptr_t)conn_encs;

        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            free(modes); free(conn_encs); continue;
        }

        if (conn.connection == DRM_MODE_CONNECTED && conn.count_modes > 0) {
            connector_id = conn.connector_id;
            mode = modes[0]; // first mode
            encoder_id = conn.encoder_id ? conn.encoder_id :
                         (conn.count_encoders ? conn_encs[0] : 0);
            free(modes); free(conn_encs);
            break;
        }

        free(modes); free(conn_encs);
    }

    if (!connector_id) fatal("no connected connector found");

    // 3) Get encoder, choose CRTC
    struct drm_mode_get_encoder enc = {0};
    enc.encoder_id = encoder_id;
    if (ioctl(drm, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
        fatal("GETENCODER failed: %m");

    uint32_t crtc_id = pick_crtc_id(&res, &enc);

    // 4) Create dumb buffer
    struct drm_mode_create_dumb creq = {0};
    creq.width  = mode.hdisplay;
    creq.height = mode.vdisplay;
    creq.bpp    = 32;
    if (ioctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
        fatal("CREATE_DUMB %ux%u@32 failed: %m", creq.width, creq.height);

    // 5) FB
    struct drm_mode_fb_cmd2 fb = {0};
    fb.width  = creq.width;
    fb.height = creq.height;
    fb.pixel_format = DRM_FORMAT_XRGB8888;
    fb.pitches[0] = creq.pitch;
    fb.handles[0] = creq.handle;
    fb.offsets[0] = 0;
    if (ioctl(drm, DRM_IOCTL_MODE_ADDFB2, &fb) < 0)
        fatal("ADDFB2 failed: %m");

    // 6) Map it
    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = creq.handle;
    if (ioctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0)
        fatal("MAP_DUMB failed: %m");

    uint8_t *map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm, mreq.offset);
    if (map == MAP_FAILED) fatal("mmap dumb buffer failed: %m");

    // 7) Paint smiley
    const uint32_t black = 0x00000000;
    const uint32_t yellow= 0x00FFFF00;
    const uint32_t eye   = 0x00000000;
    const uint32_t mouth = 0x00000000;

    // clear
    for (uint32_t y = 0; y < creq.height; y++) {
        uint32_t *row = (uint32_t *)(map + y * creq.pitch);
        for (uint32_t x = 0; x < creq.width; x++) row[x] = black;
    }

    int cx = creq.width / 2, cy = creq.height / 2;
    int r  = (creq.height < creq.width ? creq.height : creq.width) / 6;

    for (int y = -r; y <= r; y++) {
        uint32_t *row = (uint32_t *)(map + (cy + y) * creq.pitch);
        for (int x = -r; x <= r; x++) {
            int px = cx + x, py = cy + y;
            if ((unsigned)px >= creq.width || (unsigned)py >= creq.height) continue;

            int d2 = x*x + y*y;
            if (d2 <= r*r) row[px] = yellow;

            int ex = r/2, ey = -r/3, er = r/10;
            int d2L = (x + ex)*(x + ex) + (y + ey)*(y + ey);
            int d2R = (x - ex)*(x - ex) + (y + ey)*(y + ey);
            if (d2L <= er*er || d2R <= er*er) row[px] = eye;

            if (y > 0) {
                int mr = r/2;
                int d2i = x*x + (y - mr/3)*(y - mr/3);
                if (d2i >= (mr-2)*(mr-2) && d2i <= (mr+2)*(mr+2)) row[px] = mouth;
            }
        }
    }

    // 8) Program CRTC
    struct drm_mode_crtc crtc = {0};
    crtc.crtc_id = crtc_id;
    crtc.fb_id   = fb.fb_id;
    crtc.set_connectors_ptr = (uintptr_t)&connector_id;
    crtc.count_connectors   = 1;
    crtc.mode = mode;
    crtc.mode_valid = 1;

    if (ioctl(drm, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
        fatal("SETCRTC failed: %m");

    logc("init: smiley shown %ux%u pitch=%u fb=%u crtc=%u conn=%u\n",
         creq.width, creq.height, creq.pitch, fb.fb_id, crtc_id, connector_id);

    signal(SIGINT, SIG_IGN);
    for (;;) pause();
}
