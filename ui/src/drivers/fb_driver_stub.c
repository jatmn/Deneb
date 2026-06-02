/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Stub framebuffer driver for host-side build testing.
 * On the real target, fb_driver.c (with Linux fb.h) is used instead.
 */

#include "fb_driver.h"
#include <stdio.h>
#include <string.h>
#include "lvgl.h"

static lv_display_t *disp = NULL;
static uint16_t frame[320 * 240];

static void fb_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    const lv_color_format_t cf = lv_display_get_color_format(d);
    const uint32_t px_size = lv_color_format_get_size(cf);
    const int32_t row_width = lv_area_get_width(area);
    const uint32_t row_bytes = row_width * px_size;
    const uint32_t src_stride =
        lv_draw_buf_width_to_stride((uint32_t)row_width, cf);
    const uint8_t *src_row = px_map;

    for (int32_t y = area->y1; y <= area->y2; y++) {
        uint16_t *dst = &frame[(size_t)y * 320U + (size_t)area->x1];
        memcpy(dst, src_row, row_bytes);
        src_row += src_stride;
    }

    lv_display_flush_ready(d);
}

int fb_driver_init(void)
{
    memset(frame, 0, sizeof(frame));

    disp = lv_display_create(320, 240);
    if (!disp) return -1;

    static lv_color_t buf[320 * 40];
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, fb_flush_cb);
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    fprintf(stderr, "fb_driver: stub initialized (host build)\n");
    return 0;
}

void fb_driver_deinit(void)
{
    if (disp) {
        lv_display_delete(disp);
        disp = NULL;
    }
}

int fb_driver_save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "P6\n320 240\n255\n");
    for (size_t i = 0; i < 320U * 240U; i++) {
        uint16_t c = frame[i];
        unsigned char rgb[3] = {
            (unsigned char)(((c >> 11) & 0x1f) * 255 / 31),
            (unsigned char)(((c >> 5) & 0x3f) * 255 / 63),
            (unsigned char)((c & 0x1f) * 255 / 31),
        };
        fwrite(rgb, 1, sizeof(rgb), f);
    }

    return fclose(f) == 0 ? 0 : -1;
}
