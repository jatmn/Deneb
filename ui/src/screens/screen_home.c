/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Home screen - main menu. LVGL v9.
 * Vertical list layout for 320x240 - 6 items, scrollable.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

extern const screen_ops_t screen_status;
extern const screen_ops_t screen_print;
extern const screen_ops_t screen_material;
extern const screen_ops_t screen_maintenance;
extern const screen_ops_t screen_jog;
extern const screen_ops_t screen_temp;
extern const screen_ops_t screen_settings;

static lv_obj_t *home_screen = NULL;

static void btn_click_cb(lv_event_t *e)
{
    const screen_ops_t *target = (const screen_ops_t *)lv_event_get_user_data(e);
    if (target)
        screen_mgr_push(target);
}

static void create_menu_item(lv_obj_t *parent, const char *icon,
                             const char *text, const screen_ops_t *target)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 296, 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_pad_hor(btn, 8, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, (void *)target);

    lv_obj_t *icon_lbl = lv_label_create(btn);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(icon_lbl, &deneb_font_14, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *text_lbl = lv_label_create(btn);
    lv_label_set_text(text_lbl, text);
    lv_obj_set_style_text_color(text_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(text_lbl, &deneb_font_14, 0);
    lv_obj_align(text_lbl, LV_ALIGN_LEFT_MID, 28, 0);
}

static lv_obj_t *home_create(void)
{
    home_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(home_screen, 320, 208);
    lv_obj_align(home_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(home_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(home_screen, 0, 0);
    lv_obj_set_style_border_width(home_screen, 0, 0);
    lv_obj_set_style_pad_all(home_screen, 8, 0);

    lv_obj_set_flex_flow(home_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(home_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(home_screen, 5, 0);
    lv_obj_set_scroll_dir(home_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(home_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(home_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(home_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    create_menu_item(home_screen, LV_SYMBOL_HOME,
                     locale_get("menu.status"), &screen_status);
    create_menu_item(home_screen, LV_SYMBOL_FILE,
                     locale_get("menu.print"), &screen_print);
    create_menu_item(home_screen, LV_SYMBOL_LOOP,
                     locale_get("menu.material"), &screen_material);
    create_menu_item(home_screen, LV_SYMBOL_SETTINGS,
                     locale_get("menu.maintenance"), &screen_maintenance);
    create_menu_item(home_screen, LV_SYMBOL_DRIVE,
                     locale_get("menu.jog"), &screen_jog);
    create_menu_item(home_screen, LV_SYMBOL_WARNING,
                     locale_get("menu.temp"), &screen_temp);
    create_menu_item(home_screen, LV_SYMBOL_SETTINGS,
                     locale_get("menu.settings"), &screen_settings);

    return home_screen;
}

static void home_destroy(void)
{
    home_screen = NULL;
}

const screen_ops_t screen_home = {
    .name = "app.name",
    .create = home_create,
    .destroy = home_destroy,
    .show_back = false,
};
