/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Frame lighting controls.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "gcode_command.h"
#include "print_state_rules.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_LIGHT_DEFAULT_BRIGHTNESS 100

static lv_obj_t *frame_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *brightness_label = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_timer_t *startup_apply_timer = NULL;

static int saved_enabled = 0;
static int saved_brightness = FRAME_LIGHT_DEFAULT_BRIGHTNESS;
static int startup_apply_attempts = 0;

#define STARTUP_APPLY_SETTLE_ATTEMPTS 5
#define STARTUP_APPLY_MAX_ATTEMPTS    180

static int clamp_brightness(int value)
{
    if (value < 0)
        return 0;
    if (value > 100)
        return 100;
    return value;
}

static int read_uci_int(const char *key, int fallback)
{
    char cmd[128];
    char buf[32];
    FILE *f;

    snprintf(cmd, sizeof(cmd), "uci -q get %s 2>/dev/null", key);
    f = popen(cmd, "r");
    if (!f)
        return fallback;
    if (!fgets(buf, sizeof(buf), f)) {
        pclose(f);
        return fallback;
    }
    pclose(f);

    return atoi(buf);
}

static void load_saved_light_state(void)
{
    int legacy = read_uci_int("ultimaker.option.framelight", -1);
    int brightness =
        read_uci_int("deneb.frame_light.brightness",
                     legacy >= 0 ? legacy : FRAME_LIGHT_DEFAULT_BRIGHTNESS);
    int enabled = read_uci_int("deneb.frame_light.enabled",
                               legacy > 0 ? 1 : 0);

    saved_brightness = clamp_brightness(brightness);
    if (saved_brightness == 0)
        saved_brightness = FRAME_LIGHT_DEFAULT_BRIGHTNESS;
    saved_enabled = enabled ? 1 : 0;
}

static void save_light_state(void)
{
    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "uci -q set deneb.frame_light=frame_light; "
             "uci -q set deneb.frame_light.enabled='%d'; "
             "uci -q set deneb.frame_light.brightness='%d'; "
             "uci -q set ultimaker.option.framelight='%d'; "
             "uci -q commit deneb; "
             "uci -q commit ultimaker",
             saved_enabled, saved_brightness,
             saved_enabled ? saved_brightness : 0);
    system(cmd);
}

static int apply_light_pwm(int brightness)
{
    char gcode[32];

    if (deneb_gcode_format_frame_light(brightness, gcode, sizeof(gcode)) < 0)
        return -1;
    return backend_send_gcode(gcode);
}

static void update_brightness_label(void)
{
    if (brightness_label)
        lv_label_set_text_fmt(brightness_label,
                              locale_get("frame_lighting.brightness_fmt"),
                              saved_brightness);
}

static void update_status_label(void)
{
    if (!status_label)
        return;

    lv_label_set_text_fmt(status_label,
                          locale_get("frame_lighting.status_fmt"),
                          saved_enabled ? locale_get("diagnostics.on")
                                        : locale_get("diagnostics.off"),
                          saved_brightness);
}

static void light_cb(lv_event_t *e)
{
    int enabled = (int)(intptr_t)lv_event_get_user_data(e);
    int output_brightness;

    saved_enabled = enabled ? 1 : 0;
    output_brightness = saved_enabled ? saved_brightness : 0;
    if (apply_light_pwm(output_brightness) == 0) {
        save_light_state();
        update_status_label();
    } else if (status_label) {
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
    }
}

static void brightness_slider_cb(lv_event_t *e)
{
    saved_brightness = clamp_brightness(lv_slider_get_value(lv_event_get_target(e)));
    update_brightness_label();
}

static void brightness_slider_release_cb(lv_event_t *e)
{
    (void)e;
    if (saved_enabled && apply_light_pwm(saved_brightness) != 0) {
        if (status_label)
            lv_label_set_text(status_label, locale_get("settings.save_failed"));
        return;
    }

    save_light_state();
    update_status_label();
}

static void make_btn(lv_obj_t *parent, const char *label, int enabled)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 136, 38);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, light_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)enabled);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_center(lbl);
}

static lv_obj_t *frame_create(void)
{
    load_saved_light_state();

    frame_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(frame_screen, 320, 208);
    lv_obj_align(frame_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(frame_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(frame_screen, 0, 0);
    lv_obj_set_style_border_width(frame_screen, 0, 0);
    lv_obj_set_style_pad_all(frame_screen, 12, 0);
    lv_obj_set_flex_flow(frame_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(frame_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(frame_screen, 10, 0);

    brightness_label = lv_label_create(frame_screen);
    lv_obj_set_width(brightness_label, 292);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(brightness_label, &deneb_font_14, 0);
    lv_obj_set_style_text_align(brightness_label, LV_TEXT_ALIGN_CENTER, 0);
    update_brightness_label();

    brightness_slider = lv_slider_create(frame_screen);
    lv_obj_set_size(brightness_slider, 280, 18);
    lv_slider_set_range(brightness_slider, 1, 100);
    lv_slider_set_value(brightness_slider, saved_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x53a8b6),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xe0e0e0),
                              LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_release_cb,
                        LV_EVENT_RELEASED, NULL);

    lv_obj_t *actions = lv_obj_create(frame_screen);
    lv_obj_set_size(actions, 292, 42);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_btn(actions, locale_get("frame_lighting.on"), 1);
    make_btn(actions, locale_get("frame_lighting.off"), 0);

    status_label = lv_label_create(frame_screen);
    lv_obj_set_width(status_label, 292);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);
    update_status_label();

    return frame_screen;
}

static void frame_destroy(void)
{
    frame_screen = NULL;
    status_label = NULL;
    brightness_label = NULL;
    brightness_slider = NULL;
}

static void startup_apply_timer_cb(lv_timer_t *timer)
{
    const printer_state_t *state = backend_get_state();

    startup_apply_attempts++;
    if (startup_apply_attempts < STARTUP_APPLY_SETTLE_ATTEMPTS)
        return;

    if (!state ||
        !deneb_print_manual_action_allowed(state->connected, state->has_error,
                                           state->is_paused,
                                           state->is_printing)) {
        if (startup_apply_attempts < STARTUP_APPLY_MAX_ATTEMPTS)
            return;

        lv_timer_delete(timer);
        startup_apply_timer = NULL;
        return;
    }

    load_saved_light_state();
    if (apply_light_pwm(saved_enabled ? saved_brightness : 0) == 0 ||
        startup_apply_attempts >= STARTUP_APPLY_MAX_ATTEMPTS) {
        lv_timer_delete(timer);
        startup_apply_timer = NULL;
    }
}

void frame_lighting_schedule_saved_apply(void)
{
    if (startup_apply_timer)
        return;

    startup_apply_attempts = 0;
    startup_apply_timer = lv_timer_create(startup_apply_timer_cb, 1000, NULL);
}

const screen_ops_t screen_frame_lighting = {
    .name = "settings.frame_lighting",
    .create = frame_create,
    .destroy = frame_destroy,
    .show_back = true,
};
