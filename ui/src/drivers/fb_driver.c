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
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lvgl.h"

#define FB_DEVICE   "/dev/fb0"
#define FB_WIDTH    320
#define FB_HEIGHT   240
#define FB_BPP      16

static int fb_fd = -1;
static uint8_t *fb_mem = NULL;
static size_t fb_size = 0;
static uint32_t fb_stride = FB_WIDTH * (FB_BPP / 8);
static bool fb_bgr565 = false;

/* Partial render buffer: 80 lines = 51.2KB, reducing flush chunks per frame. */
static lv_color_t buf1[FB_WIDTH * 80];

static lv_display_t *disp = NULL;

static void fb_sync_area(const lv_area_t *area)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        msync(fb_mem, fb_size, MS_ASYNC);
        return;
    }

    size_t start = (size_t)area->y1 * fb_stride;
    size_t end = ((size_t)area->y2 + 1U) * fb_stride;
    if (end > fb_size)
        end = fb_size;

    size_t page_mask = (size_t)page_size - 1U;
    size_t aligned_start = start & ~page_mask;
    size_t aligned_end = (end + page_mask) & ~page_mask;
    if (aligned_end > fb_size)
        aligned_end = fb_size;

    if (aligned_end > aligned_start)
        msync(fb_mem + aligned_start, aligned_end - aligned_start, MS_ASYNC);
}

static uint16_t rgb565_to_bgr565(uint16_t c)
{
    return (uint16_t)(((c & 0xf800U) >> 11) |
                      (c & 0x07e0U) |
                      ((c & 0x001fU) << 11));
}

/**
 * LVGL v9 flush callback: copy rendered pixels to the mmap'd framebuffer.
 */
static void fb_flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map)
{
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const uint32_t px_size = lv_color_format_get_size(cf);
    const int32_t row_width = lv_area_get_width(area);
    const uint32_t row_bytes = row_width * px_size;
    const uint32_t src_stride =
        lv_draw_buf_width_to_stride((uint32_t)row_width, cf);
    uint8_t *src_row = px_map;

    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint16_t *fb_ptr = (uint16_t *)(fb_mem + y * fb_stride +
                                        area->x1 * px_size);
        if (fb_bgr565) {
            const lv_color_t *color_p = (const lv_color_t *)src_row;
            for (int32_t x = 0; x < row_width; x++) {
                uint16_t c;
                memcpy(&c, &color_p[x], sizeof(c));
                fb_ptr[x] = rgb565_to_bgr565(c);
            }
        } else {
            memcpy(fb_ptr, src_row, row_bytes);
        }
        src_row += src_stride;
    }

    fb_sync_area(area);
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

    fb_stride = finfo.line_length;
    fb_bgr565 = vinfo.red.offset == 0 && vinfo.blue.offset == 11;

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

    fprintf(stderr,
            "fb_driver: initialized %dx%d %s565 on %s stride=%u size=%zu "
            "r=%u:%u g=%u:%u b=%u:%u\n",
            FB_WIDTH, FB_HEIGHT, fb_bgr565 ? "BGR" : "RGB", FB_DEVICE,
            fb_stride, fb_size,
            vinfo.red.offset, vinfo.red.length,
            vinfo.green.offset, vinfo.green.length,
            vinfo.blue.offset, vinfo.blue.length);

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
