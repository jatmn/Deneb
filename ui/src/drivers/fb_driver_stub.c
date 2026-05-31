/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Stub framebuffer driver for host-side build testing.
 * On the real target, fb_driver.c (with Linux fb.h) is used instead.
 */

#include "fb_driver.h"
#include <stdio.h>
#include "lvgl.h"

static lv_display_t *disp = NULL;

static void fb_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    /* Stub: discard rendered pixels */
    (void)area;
    (void)px_map;
    lv_display_flush_ready(d);
}

int fb_driver_init(void)
{
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
