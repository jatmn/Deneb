/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Material screen - load/unload via coordinator macros. LVGL v9.
 *
 * Macro files on device (from /home/cygnus/marlindriver/gcode/):
 *   material_down.gcode        - Feed material down (unload)
 *   move_material_up.gcode     - Move material up (load)
 *   move_material_finish.gcode - Finish material operation
 *   retract.gcode              - Retract filament
 *
 * Commands go through coordinator port 5566:
 *   MACRO<{"macro":"material_down.gcode"}
 *   GCODE<["M104 S210"]
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *material_screen = NULL;
static lv_obj_t *status_label = NULL;

extern const screen_ops_t screen_set_material;

static int material_actions_allowed(void)
{
    const printer_state_t *s = backend_get_state();
    return s && s->connected && !s->is_printing && !s->is_paused &&
           !s->has_error;
}

static void load_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!material_actions_allowed()) {
        lv_label_set_text(status_label, locale_get("material.busy"));
        return;
    }

    /* Heat nozzle to 210C then move material up */
    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"move_material_up.gcode\"}");
    lv_label_set_text(status_label, locale_get("material.loading"));
}

static void unload_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!material_actions_allowed()) {
        lv_label_set_text(status_label, locale_get("material.busy"));
        return;
    }

    /* Heat nozzle to 210C then move material down */
    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"material_down.gcode\"}");
    lv_label_set_text(status_label, locale_get("material.unloading"));
}

static void change_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!material_actions_allowed()) {
        lv_label_set_text(status_label, locale_get("material.busy"));
        return;
    }

    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"material_down.gcode\"}");
    lv_label_set_text(status_label, locale_get("material.change_started"));
}

static void move_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!material_actions_allowed()) {
        lv_label_set_text(status_label, locale_get("material.busy"));
        return;
    }

    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"home_and_center_head.gcode\"}");
    backend_send_command("MACRO", "{\"macro\":\"move_material_up.gcode\"}");
    lv_label_set_text(status_label, locale_get("material.moving"));
}

static void finish_btn_cb(lv_event_t *e)
{
    (void)e;
    backend_send_command("MACRO", "{\"macro\":\"move_material_finish.gcode\"}");
    lv_label_set_text(status_label, locale_get("material.move_finished"));
}

static void set_material_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_set_material);
}

static void import_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_set_material);
}

static void create_action_btn(lv_obj_t *parent, const char *label,
                              lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 280, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
}

static lv_obj_t *material_create(void)
{
    material_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(material_screen, 320, 208);
    lv_obj_align(material_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(material_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(material_screen, 0, 0);
    lv_obj_set_style_border_width(material_screen, 0, 0);
    lv_obj_set_style_pad_all(material_screen, 10, 0);
    lv_obj_set_flex_flow(material_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(material_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(material_screen, 6, 0);
    lv_obj_set_scroll_dir(material_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(material_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(material_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(material_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *info_label = lv_label_create(material_screen);
    lv_label_set_text(info_label, locale_get("material.insert_material"));
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(info_label, &deneb_font_14, 0);
    lv_obj_set_width(info_label, 280);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);

    create_action_btn(material_screen, locale_get("material.change"),
                      change_btn_cb);
    create_action_btn(material_screen, locale_get("material.load"),
                      load_btn_cb);
    create_action_btn(material_screen, locale_get("material.unload"),
                      unload_btn_cb);
    create_action_btn(material_screen, locale_get("material.set"),
                      set_material_btn_cb);
    create_action_btn(material_screen, locale_get("material.move"),
                      move_btn_cb);
    create_action_btn(material_screen, locale_get("material.finish_move"),
                      finish_btn_cb);
    create_action_btn(material_screen, locale_get("material.import"),
                      import_btn_cb);

    status_label = lv_label_create(material_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    return material_screen;
}

static void material_destroy(void)
{
    material_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_material = {
    .name = "menu.material",
    .create = material_create,
    .destroy = material_destroy,
    .show_back = true,
};
