/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Temperature control screen - wired to backend. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "gcode_command.h"
#include "print_state_rules.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *temp_screen = NULL;
static lv_obj_t *nozzle_cur_label = NULL;
static lv_obj_t *nozzle_target_label = NULL;
static lv_obj_t *bed_cur_label = NULL;
static lv_obj_t *bed_target_label = NULL;
static lv_obj_t *nozzle_slider = NULL;
static lv_obj_t *bed_slider = NULL;
static lv_timer_t *temp_timer = NULL;

#define NOZZLE_MAX_TEMP 260
#define BED_MAX_TEMP    110

static int temp_actions_allowed(void)
{
    const printer_state_t *s = backend_get_state();
    return s && deneb_print_manual_action_allowed(s->connected, s->has_error,
                                                  s->is_paused, s->is_printing);
}

static void set_celsius_label(lv_obj_t *label, float temp)
{
    char text[16];
    snprintf(text, sizeof(text), "%.0f\u00b0C", temp);
    lv_label_set_text(label, text);
}

/* Live temperature update from backend */
static void temp_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const printer_state_t *s = backend_get_state();
    if (nozzle_cur_label)
        set_celsius_label(nozzle_cur_label, s->nozzle_temp_cur);
    if (bed_cur_label)
        set_celsius_label(bed_cur_label, s->bed_temp_cur);
}

static void nozzle_slider_cb(lv_event_t *e)
{
    int temp = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(nozzle_target_label, "%d\u00b0C", temp);
}

static void bed_slider_cb(lv_event_t *e)
{
    int temp = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(bed_target_label, "%d\u00b0C", temp);
}

static void set_nozzle_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!temp_actions_allowed())
        return;

    int temp = lv_slider_get_value(nozzle_slider);
    char gcode[32];
    if (deneb_gcode_format_nozzle_target((float)temp, gcode,
                                         sizeof(gcode)) == 0)
        backend_send_gcode(gcode);
}

static void set_bed_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!temp_actions_allowed())
        return;

    int temp = lv_slider_get_value(bed_slider);
    char gcode[32];
    if (deneb_gcode_format_bed_target((float)temp, gcode,
                                      sizeof(gcode)) == 0)
        backend_send_gcode(gcode);
}

static void cooldown_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!temp_actions_allowed())
        return;

    {
        char gcode[32];
        if (deneb_gcode_format_nozzle_target(0.0f, gcode, sizeof(gcode)) == 0)
            backend_send_gcode(gcode);
        if (deneb_gcode_format_bed_target(0.0f, gcode, sizeof(gcode)) == 0)
            backend_send_gcode(gcode);
    }
    backend_send_gcode(DENEB_GCODE_FAN_OFF);
    lv_slider_set_value(nozzle_slider, 0, LV_ANIM_ON);
    lv_slider_set_value(bed_slider, 0, LV_ANIM_ON);
    lv_label_set_text(nozzle_target_label, "0\u00b0C");
    lv_label_set_text(bed_target_label, "0\u00b0C");
}

static lv_obj_t *create_temp_control(lv_obj_t *parent, const char *title,
                                     int max_temp, lv_event_cb_t slider_cb,
                                     lv_event_cb_t set_cb,
                                     lv_obj_t **out_slider,
                                     lv_obj_t **out_cur,
                                     lv_obj_t **out_target)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 300, 60);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_pad_all(panel, 6, 0);

    lv_obj_t *title_lbl = lv_label_create(panel);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(title_lbl, &deneb_font_12, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    *out_cur = lv_label_create(panel);
    lv_label_set_text(*out_cur, "---\u00b0C");
    lv_obj_set_style_text_color(*out_cur, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(*out_cur, &deneb_font_12, 0);
    lv_obj_align(*out_cur, LV_ALIGN_TOP_LEFT, 60, 0);

    *out_target = lv_label_create(panel);
    lv_label_set_text(*out_target, "0\u00b0C");
    lv_obj_set_style_text_color(*out_target, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(*out_target, &deneb_font_12, 0);
    lv_obj_align(*out_target, LV_ALIGN_TOP_RIGHT, 0, 0);

    *out_slider = lv_slider_create(panel);
    lv_obj_set_size(*out_slider, 200, 10);
    lv_obj_align(*out_slider, LV_ALIGN_BOTTOM_LEFT, 0, -2);
    lv_slider_set_range(*out_slider, 0, max_temp);
    lv_slider_set_value(*out_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(*out_slider, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(*out_slider, lv_color_hex(0xe94560), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*out_slider, lv_color_hex(0xe0e0e0), LV_PART_KNOB);
    lv_obj_add_event_cb(*out_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *set_btn = lv_button_create(panel);
    lv_obj_set_size(set_btn, 48, 20);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(set_btn, 4, 0);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    lv_obj_add_event_cb(set_btn, set_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, locale_get("temp.set"));
    lv_obj_set_style_text_font(set_lbl, &deneb_font_12, 0);
    lv_obj_center(set_lbl);

    return panel;
}

static lv_obj_t *temp_create(void)
{
    temp_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(temp_screen, 320, 208);
    lv_obj_align(temp_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(temp_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(temp_screen, 0, 0);
    lv_obj_set_style_border_width(temp_screen, 0, 0);
    lv_obj_set_style_pad_all(temp_screen, 8, 0);
    lv_obj_set_flex_flow(temp_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(temp_screen, 8, 0);

    create_temp_control(temp_screen, locale_get("temp.nozzle"), NOZZLE_MAX_TEMP,
                        nozzle_slider_cb, set_nozzle_btn_cb,
                        &nozzle_slider, &nozzle_cur_label, &nozzle_target_label);

    create_temp_control(temp_screen, locale_get("temp.bed"), BED_MAX_TEMP,
                        bed_slider_cb, set_bed_btn_cb,
                        &bed_slider, &bed_cur_label, &bed_target_label);

    /* Cooldown button */
    lv_obj_t *cooldown_btn = lv_button_create(temp_screen);
    lv_obj_set_size(cooldown_btn, 300, 32);
    lv_obj_set_style_bg_color(cooldown_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(cooldown_btn, 6, 0);
    lv_obj_add_event_cb(cooldown_btn, cooldown_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cd_lbl = lv_label_create(cooldown_btn);
    lv_label_set_text_fmt(cd_lbl, "%s %s", LV_SYMBOL_CLOSE,
                          locale_get("temp.cooldown"));
    lv_obj_center(cd_lbl);

    /* Live temp update timer */
    temp_timer = lv_timer_create(temp_timer_cb, 500, NULL);

    return temp_screen;
}

static void temp_destroy(void)
{
    if (temp_timer) {
        lv_timer_delete(temp_timer);
        temp_timer = NULL;
    }
    temp_screen = NULL;
    nozzle_cur_label = NULL;
    nozzle_target_label = NULL;
    bed_cur_label = NULL;
    bed_target_label = NULL;
    nozzle_slider = NULL;
    bed_slider = NULL;
}

const screen_ops_t screen_temp = {
    .name = "menu.temp",
    .create = temp_create,
    .destroy = temp_destroy,
    .show_back = true,
};
