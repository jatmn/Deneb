/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Diagnostics and log export screen.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "diagnostics_export.h"
#include "gcode_command.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *diagnostics_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *air_label = NULL;
static lv_obj_t *build_temp_label = NULL;
static lv_obj_t *fan_label = NULL;
static lv_timer_t *diag_timer = NULL;
static int fan_on = 0;

static void update_diag_labels(void)
{
    const printer_state_t *s = backend_get_state();
    if (!s)
        return;

    lv_label_set_text_fmt(air_label, locale_get("diagnostics.air_manager_fmt"),
                          s->topcap_present
                              ? locale_get("diagnostics.present")
                              : locale_get("diagnostics.not_present"));
    lv_label_set_text_fmt(build_temp_label, locale_get("diagnostics.build_volume_fmt"),
                          s->topcap_temp_cur);
    lv_label_set_text_fmt(fan_label, locale_get("diagnostics.fan_fmt"),
                          fan_on ? locale_get("diagnostics.on")
                                 : locale_get("diagnostics.off"));
}

static void diag_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_diag_labels();
}

static void fan_toggle_cb(lv_event_t *e)
{
    char gcode[32];
    int next_fan_on;

    (void)e;
    next_fan_on = !fan_on;
    if (deneb_gcode_format_air_manager_fan(next_fan_on, gcode,
                                           sizeof(gcode)) < 0 ||
        backend_send_gcode(gcode) != 0)
        return;
    fan_on = next_fan_on;
    update_diag_labels();
}

static int usb_export_available(void)
{
    return system(deneb_diagnostics_export_usb_available_command()) == 0;
}

static void export_logs_cb(lv_event_t *e)
{
    char cmd[2048];

    (void)e;
    if (!usb_export_available()) {
        lv_label_set_text(status_label, locale_get("diagnostics.usb_required"));
        return;
    }

    if (deneb_diagnostics_export_format_command(cmd, sizeof(cmd)) < 0) {
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
        return;
    }

    system(cmd);
    lv_label_set_text(status_label, locale_get("diagnostics.export_started"));
}

static lv_obj_t *diagnostics_create(void)
{
    diagnostics_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(diagnostics_screen, 320, 208);
    lv_obj_align(diagnostics_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(diagnostics_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(diagnostics_screen, 0, 0);
    lv_obj_set_style_border_width(diagnostics_screen, 0, 0);
    lv_obj_set_style_pad_all(diagnostics_screen, 12, 0);
    lv_obj_set_flex_flow(diagnostics_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(diagnostics_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(diagnostics_screen, 8, 0);
    lv_obj_set_scroll_dir(diagnostics_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(diagnostics_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(diagnostics_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(diagnostics_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *info = lv_label_create(diagnostics_screen);
    lv_label_set_text(info, locale_get("diagnostics.message"));
    lv_obj_set_width(info, 280);
    lv_label_set_long_mode(info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0xe0e0e0), 0);

    air_label = lv_label_create(diagnostics_screen);
    lv_label_set_text_fmt(air_label, locale_get("diagnostics.air_manager_fmt"),
                          locale_get("diagnostics.unknown"));
    lv_obj_set_style_text_color(air_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(air_label, &deneb_font_12, 0);

    build_temp_label = lv_label_create(diagnostics_screen);
    lv_label_set_text(build_temp_label, locale_get("diagnostics.build_volume_unknown"));
    lv_obj_set_style_text_color(build_temp_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(build_temp_label, &deneb_font_12, 0);

    lv_obj_t *fan_btn = lv_button_create(diagnostics_screen);
    lv_obj_set_size(fan_btn, 240, 32);
    lv_obj_set_style_bg_color(fan_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(fan_btn, 4, 0);
    lv_obj_add_event_cb(fan_btn, fan_toggle_cb, LV_EVENT_CLICKED, NULL);
    fan_label = lv_label_create(fan_btn);
    lv_label_set_text_fmt(fan_label, locale_get("diagnostics.fan_fmt"),
                          locale_get("diagnostics.off"));
    lv_obj_center(fan_label);

    lv_obj_t *btn = lv_button_create(diagnostics_screen);
    lv_obj_set_size(btn, 240, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, export_logs_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, locale_get("diagnostics.export_logs"));
    lv_obj_center(lbl);

    status_label = lv_label_create(diagnostics_screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    update_diag_labels();
    diag_timer = lv_timer_create(diag_timer_cb, 1000, NULL);

    return diagnostics_screen;
}

static void diagnostics_destroy(void)
{
    if (diag_timer) {
        lv_timer_delete(diag_timer);
        diag_timer = NULL;
    }
    diagnostics_screen = NULL;
    status_label = NULL;
    air_label = NULL;
    build_temp_label = NULL;
    fan_label = NULL;
}

const screen_ops_t screen_diagnostics = {
    .name = "maintenance.diagnostics",
    .create = diagnostics_create,
    .destroy = diagnostics_destroy,
    .show_back = true,
};
