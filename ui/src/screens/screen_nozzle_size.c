/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Nozzle size setting. Matches stock UCI option: ultimaker.option.nozzle_size.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *nozzle_screen = NULL;
static lv_obj_t *status_label = NULL;

static void size_cb(lv_event_t *e)
{
    const char *size = (const char *)lv_event_get_user_data(e);
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "uci set ultimaker.option.nozzle_size=%s && uci commit ultimaker",
             size);
    if (system(cmd) == 0)
        lv_label_set_text_fmt(status_label, "%s mm", size);
    else
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
}

static void make_btn(lv_obj_t *parent, const char *size)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 140, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, size_cb, LV_EVENT_CLICKED, (void *)size);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text_fmt(lbl, "%s mm", size);
    lv_obj_center(lbl);
}

static lv_obj_t *nozzle_create(void)
{
    nozzle_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(nozzle_screen, 320, 208);
    lv_obj_align(nozzle_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(nozzle_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(nozzle_screen, 0, 0);
    lv_obj_set_style_border_width(nozzle_screen, 0, 0);
    lv_obj_set_style_pad_all(nozzle_screen, 12, 0);
    lv_obj_set_flex_flow(nozzle_screen, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(nozzle_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(nozzle_screen, 8, 0);

    make_btn(nozzle_screen, "0.25");
    make_btn(nozzle_screen, "0.4");
    make_btn(nozzle_screen, "0.6");
    make_btn(nozzle_screen, "0.8");

    status_label = lv_label_create(nozzle_screen);
    lv_obj_set_width(status_label, 280);
    lv_label_set_text(status_label, locale_get("settings.nozzle_size_hint"));
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);

    return nozzle_screen;
}

static void nozzle_destroy(void)
{
    nozzle_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_nozzle_size = {
    .name = "settings.nozzle_size",
    .create = nozzle_create,
    .destroy = nozzle_destroy,
    .show_back = true,
};
