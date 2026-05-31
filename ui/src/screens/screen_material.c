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
        lv_label_set_text(status_label, "Printer busy or status unavailable");
        return;
    }

    /* Heat nozzle to 210C then move material up */
    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"move_material_up.gcode\"}");
    lv_label_set_text(status_label, "Loading material (210\u00b0C)...");
}

static void unload_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!material_actions_allowed()) {
        lv_label_set_text(status_label, "Printer busy or status unavailable");
        return;
    }

    /* Heat nozzle to 210C then move material down */
    backend_send_gcode("M104 S210");
    backend_send_command("MACRO", "{\"macro\":\"material_down.gcode\"}");
    lv_label_set_text(status_label, "Unloading material (210\u00b0C)...");
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
    lv_obj_set_flex_align(material_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(material_screen, 12, 0);

    lv_obj_t *info_label = lv_label_create(material_screen);
    lv_label_set_text(info_label, locale_get("material.insert_material"));
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(info_label, 280);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *load_btn = lv_button_create(material_screen);
    lv_obj_set_size(load_btn, 200, 40);
    lv_obj_set_style_bg_color(load_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(load_btn, 8, 0);
    lv_obj_add_event_cb(load_btn, load_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *load_lbl = lv_label_create(load_btn);
    lv_label_set_text_fmt(load_lbl, "%s %s", LV_SYMBOL_PLAY,
                          locale_get("material.load"));
    lv_obj_center(load_lbl);

    lv_obj_t *unload_btn = lv_button_create(material_screen);
    lv_obj_set_size(unload_btn, 200, 40);
    lv_obj_set_style_bg_color(unload_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(unload_btn, 8, 0);
    lv_obj_add_event_cb(unload_btn, unload_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *unload_lbl = lv_label_create(unload_btn);
    lv_label_set_text_fmt(unload_lbl, "%s %s", LV_SYMBOL_EJECT,
                          locale_get("material.unload"));
    lv_obj_center(unload_lbl);

    status_label = lv_label_create(material_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    return material_screen;
}

static void material_destroy(void)
{
    material_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_material = {
    .name = "Material",
    .create = material_create,
    .destroy = material_destroy,
    .show_back = true,
};
