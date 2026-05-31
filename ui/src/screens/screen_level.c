/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Build plate leveling shortcuts using stock macro files.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"

#include <stdio.h>

static lv_obj_t *level_screen = NULL;
static lv_obj_t *status_label = NULL;

static void macro_btn_cb(lv_event_t *e)
{
    const char *macro = (const char *)lv_event_get_user_data(e);
    char args[80];
    snprintf(args, sizeof(args), "{\"macro\":\"%s\"}", macro);
    if (backend_send_command("MACRO", args) == 0)
        lv_label_set_text(status_label, macro);
}

static void create_macro_btn(lv_obj_t *parent, const char *label,
                             const char *macro)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 300, 32);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, macro_btn_cb, LV_EVENT_CLICKED, (void *)macro);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static lv_obj_t *level_create(void)
{
    level_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(level_screen, 320, 208);
    lv_obj_align(level_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(level_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(level_screen, 0, 0);
    lv_obj_set_style_border_width(level_screen, 0, 0);
    lv_obj_set_style_pad_all(level_screen, 8, 0);
    lv_obj_set_flex_flow(level_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(level_screen, 5, 0);
    lv_obj_set_scroll_dir(level_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(level_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(level_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(level_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    create_macro_btn(level_screen, locale_get("level.step1"),
                     "buildplate_level_step1.gcode");
    create_macro_btn(level_screen, locale_get("level.step2"),
                     "buildplate_level_step2.gcode");
    create_macro_btn(level_screen, locale_get("level.step3"),
                     "buildplate_level_step3.gcode");
    create_macro_btn(level_screen, locale_get("level.step4"),
                     "buildplate_level_step4.gcode");
    create_macro_btn(level_screen, locale_get("level.finish"),
                     "buildplate_level_finish.gcode");

    status_label = lv_label_create(level_screen);
    lv_label_set_text(status_label, locale_get("level.message"));
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    return level_screen;
}

static void level_destroy(void)
{
    level_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_level = {
    .name = "maintenance.level_buildplate",
    .create = level_create,
    .destroy = level_destroy,
    .show_back = true,
};
