/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "lvgl.h"
#include "draw/lv_draw_private.h"
#include "draw/sw/lv_draw_sw.h"

#include <assert.h>
#include <string.h>

enum { WIDTH = 47, HEIGHT = 32 };
LV_DRAW_BUF_DEFINE_STATIC(frame, WIDTH, HEIGHT, LV_COLOR_FORMAT_RGB565);
static bool fail_mask_allocation;

void *__real_lv_malloc(size_t size);

void *__wrap_lv_malloc(size_t size)
{
    if (fail_mask_allocation) {
        assert(size == WIDTH);
        fail_mask_allocation = false;
        return NULL;
    }
    return __real_lv_malloc(size);
}

int main(void)
{
    lv_init();
    LV_DRAW_BUF_INIT_STATIC(frame);
    lv_draw_buf_clear(&frame, NULL);

    const lv_area_t area = {0, 0, WIDTH - 1, HEIGHT - 1};
    lv_layer_t layer = {
        .draw_buf = &frame,
        .buf_area = area,
        .color_format = LV_COLOR_FORMAT_RGB565,
    };
    lv_draw_task_t task = {.clip_area = area, .target_layer = &layer};
    lv_draw_border_dsc_t border;
    lv_draw_border_dsc_init(&border);
    border.radius = 8;
    border.width = 2;
    border.opa = LV_OPA_COVER;
    border.side = LV_BORDER_SIDE_FULL;
    border.color = lv_color_white();

    const void *top_edge = lv_draw_buf_goto_xy(&frame, WIDTH / 2, 0);
    uint16_t pixel;

    fail_mask_allocation = true;
    lv_draw_sw_border(&task, &border, &area);
    assert(!fail_mask_allocation);
    memcpy(&pixel, top_edge, sizeof(pixel));
    assert(pixel == 0);

    lv_draw_sw_border(&task, &border, &area);
    memcpy(&pixel, top_edge, sizeof(pixel));
    assert(pixel == UINT16_MAX);

    lv_deinit();
    return 0;
}
