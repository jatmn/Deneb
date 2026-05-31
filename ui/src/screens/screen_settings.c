/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Settings screen. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *settings_screen = NULL;

extern const screen_ops_t screen_settings;
extern const screen_ops_t screen_about;
extern const screen_ops_t screen_network;
extern const screen_ops_t screen_frame_lighting;
extern const screen_ops_t screen_nozzle_size;
extern const screen_ops_t screen_factory_reset;
extern const screen_ops_t screen_digital_factory;
extern const screen_ops_t screen_language;

static void about_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_about);
}

static void nav_btn_cb(lv_event_t *e)
{
    const screen_ops_t *target = (const screen_ops_t *)lv_event_get_user_data(e);
    if (target)
        screen_mgr_push(target);
}

static void create_nav_btn(lv_obj_t *parent, const char *label,
                           const screen_ops_t *target)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 300, 32);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, (void *)target);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static lv_obj_t *settings_create(void)
{
    settings_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(settings_screen, 320, 208);
    lv_obj_align(settings_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(settings_screen, 0, 0);
    lv_obj_set_style_border_width(settings_screen, 0, 0);
    lv_obj_set_style_pad_all(settings_screen, 10, 0);
    lv_obj_set_flex_flow(settings_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(settings_screen, 6, 0);
    lv_obj_set_scroll_dir(settings_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(settings_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(settings_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    create_nav_btn(settings_screen, locale_get("settings.language"),
                   &screen_language);
    create_nav_btn(settings_screen, locale_get("settings.nozzle_size"),
                   &screen_nozzle_size);
    create_nav_btn(settings_screen, locale_get("settings.network"),
                   &screen_network);
    create_nav_btn(settings_screen, locale_get("settings.digital_factory"),
                   &screen_digital_factory);
    create_nav_btn(settings_screen, locale_get("settings.frame_lighting"),
                   &screen_frame_lighting);
    create_nav_btn(settings_screen, locale_get("settings.factory_reset"),
                   &screen_factory_reset);

    lv_obj_t *about_btn = lv_button_create(settings_screen);
    lv_obj_set_size(about_btn, 300, 36);
    lv_obj_set_style_bg_color(about_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(about_btn, 4, 0);
    lv_obj_add_event_cb(about_btn, about_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *about_lbl = lv_label_create(about_btn);
    lv_label_set_text_fmt(about_lbl, "%s %s", LV_SYMBOL_LIST,
                          locale_get("settings.about"));
    lv_obj_align(about_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    return settings_screen;
}

static void settings_destroy(void) { settings_screen = NULL; }

const screen_ops_t screen_settings = {
    .name = "menu.settings",
    .create = settings_create,
    .destroy = settings_destroy,
    .show_back = true,
};
