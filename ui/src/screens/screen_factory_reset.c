/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Factory reset entry point.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <stdlib.h>

static lv_obj_t *reset_screen = NULL;
static lv_obj_t *status_label = NULL;
static int reset_armed = 0;

static void reset_cb(lv_event_t *e)
{
    (void)e;
    if (!reset_armed) {
        reset_armed = 1;
        lv_label_set_text(status_label, locale_get("factory_reset.tap_again"));
        return;
    }

    lv_label_set_text(status_label, locale_get("factory_reset.started"));
    reset_armed = 0;
    system("(sleep 3; yes | /sbin/firstboot && /sbin/reboot now) "
           ">/tmp/deneb-factory-reset.log 2>&1 &");
}

static lv_obj_t *reset_create(void)
{
    reset_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(reset_screen, 320, 208);
    lv_obj_align(reset_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(reset_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(reset_screen, 0, 0);
    lv_obj_set_style_border_width(reset_screen, 0, 0);
    lv_obj_set_style_pad_all(reset_screen, 12, 0);
    lv_obj_set_flex_flow(reset_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(reset_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(reset_screen, 12, 0);

    lv_obj_t *msg = lv_label_create(reset_screen);
    lv_label_set_text(msg, locale_get("factory_reset.warning"));
    lv_obj_set_width(msg, 280);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xe0e0e0), 0);

    lv_obj_t *btn = lv_button_create(reset_screen);
    lv_obj_set_size(btn, 240, 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, locale_get("settings.factory_reset"));
    lv_obj_center(lbl);

    status_label = lv_label_create(reset_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);

    return reset_screen;
}

static void reset_destroy(void)
{
    reset_screen = NULL;
    status_label = NULL;
    reset_armed = 0;
}

const screen_ops_t screen_factory_reset = {
    .name = "settings.factory_reset",
    .create = reset_create,
    .destroy = reset_destroy,
    .show_back = true,
};
