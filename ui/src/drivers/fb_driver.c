/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Linux framebuffer driver for ILI9341 SPI TFT (/dev/fb0)
 * Direct mmap - no intermediate copy, no double buffer.
 *
 * LVGL v9 API.
 */

#include "fb_driver.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lvgl.h"

#define FB_DEVICE   "/dev/fb0"
#define FB_WIDTH    320
#define FB_HEIGHT   240
#define FB_BPP      16
#define FB_STRIDE   (FB_WIDTH * (FB_BPP / 8))  /* 640 bytes per line */

static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static size_t fb_size = 0;

/* Partial render buffer: 40 lines = 25.6KB */
static lv_color_t buf1[FB_WIDTH * 40];

static lv_display_t *disp = NULL;

/**
 * LVGL v9 flush callback: copy rendered pixels to the mmap'd framebuffer.
 */
static void fb_flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map)
{
    int32_t y;
    uint16_t *fb_ptr;
    lv_color_t *color_p = (lv_color_t *)px_map;

    for (y = area->y1; y <= area->y2; y++) {
        fb_ptr = (uint16_t *)(fb_mem + y * FB_STRIDE + area->x1 * 2);
        memcpy(fb_ptr, color_p, (area->x2 - area->x1 + 1) * 2);
        color_p += (area->x2 - area->x1 + 1);
    }

    lv_display_flush_ready(disp);
}

int fb_driver_init(void)
{
    fb_fd = open(FB_DEVICE, O_RDWR);
    if (fb_fd < 0) {
        perror("fb_driver: open " FB_DEVICE);
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("fb_driver: FBIOGET_VSCREENINFO");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("fb_driver: FBIOGET_FSCREENINFO");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    if (vinfo.xres != FB_WIDTH || vinfo.yres != FB_HEIGHT ||
        vinfo.bits_per_pixel != FB_BPP) {
        fprintf(stderr, "fb_driver: unexpected fb0 format: %ux%u @ %ubpp\n",
                vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    fb_size = finfo.smem_len;
    fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("fb_driver: mmap");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    /* Clear framebuffer to black */
    memset(fb_mem, 0, fb_size);

    /* Create LVGL v9 display */
    disp = lv_display_create(FB_WIDTH, FB_HEIGHT);
    if (!disp) {
        fprintf(stderr, "fb_driver: lv_display_create failed\n");
        munmap(fb_mem, fb_size);
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, fb_flush_cb);
    lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    fprintf(stderr, "fb_driver: initialized %dx%d RGB565 on %s\n",
            FB_WIDTH, FB_HEIGHT, FB_DEVICE);

    return 0;
}

void fb_driver_deinit(void)
{
    if (disp) {
        lv_display_delete(disp);
        disp = NULL;
    }
    if (fb_mem && fb_mem != MAP_FAILED) {
        munmap(fb_mem, fb_size);
        fb_mem = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
}
