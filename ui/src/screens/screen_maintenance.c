/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Maintenance menu. Restores stock maintenance entry points in native LVGL.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

extern const screen_ops_t screen_temp;
extern const screen_ops_t screen_update;
extern const screen_ops_t screen_jog;
extern const screen_ops_t screen_level;
extern const screen_ops_t screen_diagnostics;

static lv_obj_t *maintenance_screen = NULL;

static void nav_btn_cb(lv_event_t *e)
{
    const screen_ops_t *target = (const screen_ops_t *)lv_event_get_user_data(e);
    if (target)
        screen_mgr_push(target);
}

static void create_item(lv_obj_t *parent, const char *label,
                        const screen_ops_t *target)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 300, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, (void *)target);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static lv_obj_t *maintenance_create(void)
{
    maintenance_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(maintenance_screen, 320, 208);
    lv_obj_align(maintenance_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(maintenance_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(maintenance_screen, 0, 0);
    lv_obj_set_style_border_width(maintenance_screen, 0, 0);
    lv_obj_set_style_pad_all(maintenance_screen, 8, 0);
    lv_obj_set_flex_flow(maintenance_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(maintenance_screen, 6, 0);
    lv_obj_set_scroll_dir(maintenance_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(maintenance_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(maintenance_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(maintenance_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    create_item(maintenance_screen, locale_get("maintenance.temperature"),
                &screen_temp);
    create_item(maintenance_screen, locale_get("maintenance.update_firmware"),
                &screen_update);
    create_item(maintenance_screen, locale_get("maintenance.move_buildplate"),
                &screen_jog);
    create_item(maintenance_screen, locale_get("maintenance.level_buildplate"),
                &screen_level);
    create_item(maintenance_screen, locale_get("maintenance.diagnostics"),
                &screen_diagnostics);

    return maintenance_screen;
}

static void maintenance_destroy(void)
{
    maintenance_screen = NULL;
}

const screen_ops_t screen_maintenance = {
    .name = "menu.maintenance",
    .create = maintenance_create,
    .destroy = maintenance_destroy,
    .show_back = true,
};
