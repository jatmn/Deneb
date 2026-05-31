/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Frame lighting controls.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *frame_screen = NULL;
static lv_obj_t *status_label = NULL;

static void light_cb(lv_event_t *e)
{
    const char *macro = (const char *)lv_event_get_user_data(e);
    char args[64];
    snprintf(args, sizeof(args), "{\"macro\":\"%s\"}", macro);
    if (backend_send_command("MACRO", args) == 0) {
        const char *value = strcmp(macro, "framelight_on.gcode") == 0
                                ? "100"
                                : "0";
        char cmd[96];
        snprintf(cmd, sizeof(cmd),
                 "uci -q set ultimaker.option.framelight=%s; "
                 "uci -q commit ultimaker",
                 value);
        system(cmd);
        lv_label_set_text(status_label, macro);
    }
}

static void make_btn(lv_obj_t *parent, const char *label, const char *macro)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 240, 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, light_cb, LV_EVENT_CLICKED, (void *)macro);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
}

static lv_obj_t *frame_create(void)
{
    frame_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(frame_screen, 320, 208);
    lv_obj_align(frame_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(frame_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(frame_screen, 0, 0);
    lv_obj_set_style_border_width(frame_screen, 0, 0);
    lv_obj_set_style_pad_all(frame_screen, 12, 0);
    lv_obj_set_flex_flow(frame_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(frame_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(frame_screen, 12, 0);

    make_btn(frame_screen, locale_get("frame_lighting.on"),
             "framelight_on.gcode");
    make_btn(frame_screen, locale_get("frame_lighting.off"),
             "framelight_off.gcode");

    status_label = lv_label_create(frame_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);

    return frame_screen;
}

static void frame_destroy(void)
{
    frame_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_frame_lighting = {
    .name = "settings.frame_lighting",
    .create = frame_create,
    .destroy = frame_destroy,
    .show_back = true,
};
