/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Touchscreen input driver for the UM2C touch panel.
 * Uses LVGL's Linux evdev backend, matching the stock UI architecture.
 */

#include "touch_driver.h"

#include <stdio.h>

#include "lvgl.h"
#include "lvgl/drivers/indev/lv_evdev.h"

#define TOUCH_DEVICE          "/dev/input/event0"
#define TOUCH_READ_PERIOD_MS  8
#define TOUCH_SCROLL_LIMIT_PX 6
#define TOUCH_SCROLL_THROW    10
#define TOUCH_MIN_X           0
#define TOUCH_MAX_X           319
#define TOUCH_MIN_Y           -5
#define TOUCH_MAX_Y           234
#define TOUCH_DOT_TIMEOUT_MS  60000

static lv_indev_t *indev = NULL;
static lv_obj_t *touch_dot = NULL;
static lv_timer_t *touch_dot_timer = NULL;

static void touch_dot_timeout_cb(lv_timer_t *timer)
{
    (void)timer;
    if (touch_dot)
        lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
}

static void touch_marker_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *event_indev = lv_event_get_target(e);

    if (!touch_dot || !event_indev)
        return;

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
        code == LV_EVENT_RELEASED) {
        lv_point_t p;
        lv_indev_get_point(event_indev, &p);
        lv_obj_set_pos(touch_dot, p.x - 4, p.y - 4);
        lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
        if (touch_dot_timer)
            lv_timer_reset(touch_dot_timer);
    }
}

static void create_touch_marker(void)
{
    touch_dot = lv_obj_create(lv_layer_top());
    lv_obj_set_size(touch_dot, 9, 9);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(touch_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(touch_dot, 0, 0);
    lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(touch_dot, LV_OBJ_FLAG_HIDDEN);
    touch_dot_timer = lv_timer_create(touch_dot_timeout_cb,
                                      TOUCH_DOT_TIMEOUT_MS, NULL);
}

int touch_driver_init(void)
{
    indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, TOUCH_DEVICE);
    if (!indev) {
        fprintf(stderr, "touch_driver: failed to open %s\n", TOUCH_DEVICE);
        return -1;
    }

    lv_evdev_set_swap_axes(indev, true);
    lv_evdev_set_calibration(indev, TOUCH_MIN_X, TOUCH_MIN_Y,
                             TOUCH_MAX_X, TOUCH_MAX_Y);
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_READ_PERIOD_MS);
    lv_indev_set_scroll_limit(indev, TOUCH_SCROLL_LIMIT_PX);
    lv_indev_set_scroll_throw(indev, TOUCH_SCROLL_THROW);
    lv_indev_add_event_cb(indev, touch_marker_cb, LV_EVENT_ALL, NULL);
    create_touch_marker();

    fprintf(stderr,
            "touch_driver: initialized evdev %s read_period=%dms "
            "scroll_limit=%d scroll_throw=%d "
            "swap_axes=1 calibration=(%d,%d)-(%d,%d)\n",
            TOUCH_DEVICE, TOUCH_READ_PERIOD_MS,
            TOUCH_SCROLL_LIMIT_PX, TOUCH_SCROLL_THROW,
            TOUCH_MIN_X, TOUCH_MIN_Y, TOUCH_MAX_X, TOUCH_MAX_Y);

    return 0;
}

void touch_driver_deinit(void)
{
    if (indev) {
        lv_evdev_delete(indev);
        indev = NULL;
    }
    if (touch_dot_timer) {
        lv_timer_delete(touch_dot_timer);
        touch_dot_timer = NULL;
    }
    touch_dot = NULL;
}
