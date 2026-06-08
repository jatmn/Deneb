/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Material screen - load/change workflow via coordinator macros. LVGL v9.
 *
 * Macro files on device (from /home/cygnus/marlindriver/gcode/):
 *   material_down.gcode        - Feed material into hotend (load)
 *   move_material_up.gcode     - Retract material from hotend (unload)
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
#include "gcode_command.h"
#include "print_state_rules.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>

static lv_obj_t *material_screen = NULL;
static lv_obj_t *workflow_screen = NULL;
static lv_obj_t *workflow_status_label = NULL;
static lv_obj_t *workflow_temp_label = NULL;
static lv_obj_t *workflow_target_label = NULL;
static lv_obj_t *workflow_slider = NULL;
static lv_obj_t *workflow_load_btn = NULL;
static lv_obj_t *workflow_unload_btn = NULL;
static lv_obj_t *workflow_stop_btn = NULL;
static lv_timer_t *workflow_timer = NULL;
static int workflow_target_temp = 210;
static int workflow_moving = 0;
static uint32_t workflow_move_done_at = 0;
static int workflow_target_sent = 0;

extern const screen_ops_t screen_set_material;
extern const screen_ops_t screen_material_workflow;

#define MATERIAL_DEFAULT_TEMP 210
#define MATERIAL_MAX_TEMP     260
#define MATERIAL_MIN_MOVE_TEMP 170
#define MATERIAL_READY_WINDOW  2.0f
#define MATERIAL_MOVE_DISTANCE 360
#define MATERIAL_LOAD_FEEDRATE 60
#define MATERIAL_UNLOAD_FEEDRATE 300
#define MATERIAL_MOVE_MARGIN_MS 2000

static int material_backend_ready(void)
{
    return backend_is_ready();
}

static int material_motion_allowed(void)
{
    return backend_manual_action_allowed();
}

static int material_temp_ready(const printer_state_t *s)
{
    return s && workflow_target_temp >= MATERIAL_MIN_MOVE_TEMP &&
           deneb_print_temp_target_ready(s->nozzle_temp_cur,
                                         (float)workflow_target_temp,
                                         MATERIAL_READY_WINDOW);
}

static void set_btn_enabled(lv_obj_t *btn, int enabled)
{
    if (!btn)
        return;

    if (enabled)
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else
        lv_obj_add_state(btn, LV_STATE_DISABLED);
}

static void set_celsius_label(lv_obj_t *label, float temp)
{
    char text[16];
    snprintf(text, sizeof(text), "%.0f\u00b0C", temp);
    lv_label_set_text(label, text);
}

static void workflow_update(void)
{
    const printer_state_t *s = backend_get_state();
    int backend_ready = material_backend_ready();
    int motion_allowed = material_motion_allowed();
    int ready = material_temp_ready(s);

    if (workflow_moving && workflow_move_done_at &&
        (int32_t)(lv_tick_get() - workflow_move_done_at) >= 0) {
        workflow_moving = 0;
        workflow_move_done_at = 0;
    }

    if (workflow_temp_label && s)
        set_celsius_label(workflow_temp_label, s->nozzle_temp_cur);

    if (workflow_target_label)
        lv_label_set_text_fmt(workflow_target_label, "%d\u00b0C",
                              workflow_target_temp);

    if (workflow_status_label) {
        if (!backend_ready) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.busy"));
        } else if (workflow_moving) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.moving"));
        } else if (!workflow_target_sent && !ready) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.set_target"));
        } else if (workflow_target_temp == 0) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.cooling"));
        } else if (workflow_target_temp < MATERIAL_MIN_MOVE_TEMP) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.target_too_low"));
        } else if (ready) {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.ready_to_move"));
        } else {
            lv_label_set_text(workflow_status_label,
                              locale_get("material.heating"));
        }
    }

    set_btn_enabled(workflow_load_btn,
                    motion_allowed && ready && !workflow_moving);
    set_btn_enabled(workflow_unload_btn,
                    motion_allowed && ready && !workflow_moving);
    set_btn_enabled(workflow_stop_btn,
                    backend_ready && (workflow_moving || workflow_target_sent));
}

static void workflow_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    workflow_update();
}

static void send_material_target(void)
{
    char gcode[32];
    if (deneb_gcode_format_nozzle_target((float)workflow_target_temp, gcode,
                                         sizeof(gcode)) == 0 &&
        backend_send_gcode(gcode) == 0)
        workflow_target_sent = workflow_target_temp > 0;
}

static void workflow_slider_cb(lv_event_t *e)
{
    workflow_target_temp = lv_slider_get_value(lv_event_get_target(e));
    workflow_update();
}

static void workflow_set_temp_cb(lv_event_t *e)
{
    (void)e;

    /* Intentionally allow setting a target nozzle temperature during active
     * print flow; only actual filament motion is blocked while printing.
     */
    if (!material_backend_ready()) {
        workflow_update();
        return;
    }

    send_material_target();
    workflow_update();
}

static void workflow_load_cb(lv_event_t *e)
{
    (void)e;
    const printer_state_t *s = backend_get_state();
    char move[48];
    const char *lines[2];
    if (!material_motion_allowed() || !material_temp_ready(s)) {
        workflow_update();
        return;
    }

    lines[0] = DENEB_GCODE_RESET_EXTRUDER;
    if (deneb_gcode_format_extrude((float)MATERIAL_MOVE_DISTANCE,
                                   (float)MATERIAL_LOAD_FEEDRATE, move,
                                   sizeof(move)) < 0)
        return;
    lines[1] = move;
    if (backend_send_gcodes(lines, 2) == 0) {
        workflow_moving = 1;
        workflow_move_done_at =
            lv_tick_get() +
            (uint32_t)((MATERIAL_MOVE_DISTANCE * 60000) /
                       MATERIAL_LOAD_FEEDRATE) +
            MATERIAL_MOVE_MARGIN_MS;
    }
    workflow_update();
}

static void workflow_unload_cb(lv_event_t *e)
{
    (void)e;
    const printer_state_t *s = backend_get_state();
    char move[48];
    const char *lines[2];
    if (!material_motion_allowed() || !material_temp_ready(s)) {
        workflow_update();
        return;
    }

    lines[0] = DENEB_GCODE_RESET_EXTRUDER;
    if (deneb_gcode_format_extrude((float)-MATERIAL_MOVE_DISTANCE,
                                   (float)MATERIAL_UNLOAD_FEEDRATE, move,
                                   sizeof(move)) < 0)
        return;
    lines[1] = move;
    if (backend_send_gcodes(lines, 2) == 0) {
        workflow_moving = 1;
        workflow_move_done_at =
            lv_tick_get() +
            (uint32_t)((MATERIAL_MOVE_DISTANCE * 60000) /
                       MATERIAL_UNLOAD_FEEDRATE) +
            MATERIAL_MOVE_MARGIN_MS;
    }
    workflow_update();
}

static void workflow_cooldown_nozzle(int update_ui)
{
    workflow_target_temp = 0;
    {
        char gcode[32];
        if (deneb_gcode_format_nozzle_target(0.0f, gcode, sizeof(gcode)) == 0 &&
            backend_send_gcode(gcode) == 0)
            workflow_target_sent = 0;
    }
    if (update_ui && workflow_slider)
        lv_slider_set_value(workflow_slider, workflow_target_temp, LV_ANIM_ON);
    if (update_ui)
        workflow_update();
}

static void workflow_stop_material(int update_ui)
{
    if (workflow_moving)
        backend_send_gcode(DENEB_GCODE_STOP_MATERIAL);
    workflow_moving = 0;
    workflow_move_done_at = 0;
    workflow_cooldown_nozzle(update_ui);
}

static void workflow_stop_cb(lv_event_t *e)
{
    (void)e;
    workflow_stop_material(1);
}

static void load_change_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_material_workflow);
}

static void set_material_btn_cb(lv_event_t *e)
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

    create_action_btn(material_screen, locale_get("material.load_change"),
                      load_change_btn_cb);
    create_action_btn(material_screen, locale_get("material.set"),
                      set_material_btn_cb);

    return material_screen;
}

static void material_destroy(void)
{
    material_screen = NULL;
}

const screen_ops_t screen_material = {
    .name = "menu.material",
    .create = material_create,
    .destroy = material_destroy,
    .show_back = true,
};

static lv_obj_t *create_workflow_btn(lv_obj_t *parent, const char *label,
                                     lv_event_cb_t cb, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 88, 34);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x343448),
                              LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_center(lbl);

    return btn;
}

static lv_obj_t *material_workflow_create(void)
{
    workflow_target_temp = MATERIAL_DEFAULT_TEMP;
    workflow_moving = 0;
    workflow_move_done_at = 0;
    workflow_target_sent = 0;

    workflow_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(workflow_screen, 320, 208);
    lv_obj_align(workflow_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(workflow_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(workflow_screen, 0, 0);
    lv_obj_set_style_border_width(workflow_screen, 0, 0);
    lv_obj_set_style_pad_all(workflow_screen, 10, 0);
    lv_obj_set_flex_flow(workflow_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(workflow_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(workflow_screen, 8, 0);

    lv_obj_t *panel = lv_obj_create(workflow_screen);
    lv_obj_set_size(panel, 300, 82);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);

    lv_obj_t *nozzle_lbl = lv_label_create(panel);
    lv_label_set_text(nozzle_lbl, locale_get("temp.nozzle"));
    lv_obj_set_style_text_color(nozzle_lbl, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(nozzle_lbl, &deneb_font_12, 0);
    lv_obj_align(nozzle_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    workflow_temp_label = lv_label_create(panel);
    lv_label_set_text(workflow_temp_label, "---\u00b0C");
    lv_obj_set_style_text_color(workflow_temp_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(workflow_temp_label, &deneb_font_14, 0);
    lv_obj_align(workflow_temp_label, LV_ALIGN_TOP_LEFT, 74, -1);

    workflow_target_label = lv_label_create(panel);
    lv_label_set_text_fmt(workflow_target_label, "%d\u00b0C",
                          workflow_target_temp);
    lv_obj_set_style_text_color(workflow_target_label,
                                lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(workflow_target_label, &deneb_font_14, 0);
    lv_obj_align(workflow_target_label, LV_ALIGN_TOP_RIGHT, 0, -1);

    workflow_slider = lv_slider_create(panel);
    lv_obj_set_size(workflow_slider, 210, 12);
    lv_obj_align(workflow_slider, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_slider_set_range(workflow_slider, 0, MATERIAL_MAX_TEMP);
    lv_slider_set_value(workflow_slider, workflow_target_temp, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(workflow_slider, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(workflow_slider, lv_color_hex(0xe94560),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(workflow_slider, lv_color_hex(0xe0e0e0),
                              LV_PART_KNOB);
    lv_obj_add_event_cb(workflow_slider, workflow_slider_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *set_btn = lv_button_create(panel);
    lv_obj_set_size(set_btn, 52, 24);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(set_btn, 4, 0);
    lv_obj_align(set_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -1);
    lv_obj_add_event_cb(set_btn, workflow_set_temp_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, locale_get("temp.set"));
    lv_obj_set_style_text_font(set_lbl, &deneb_font_12, 0);
    lv_obj_center(set_lbl);

    workflow_status_label = lv_label_create(workflow_screen);
    lv_label_set_text(workflow_status_label, "");
    lv_obj_set_width(workflow_status_label, 300);
    lv_label_set_long_mode(workflow_status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(workflow_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(workflow_status_label, lv_color_hex(0xa0a0a0),
                                0);
    lv_obj_set_style_text_font(workflow_status_label, &deneb_font_12, 0);

    lv_obj_t *actions = lv_obj_create(workflow_screen);
    lv_obj_set_size(actions, 300, 38);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    workflow_load_btn = create_workflow_btn(actions,
                                            locale_get("material.load_short"),
                                            workflow_load_cb,
                                            lv_color_hex(0x1f7a5a));
    workflow_unload_btn = create_workflow_btn(actions,
                                              locale_get("material.unload_short"),
                                              workflow_unload_cb,
                                              lv_color_hex(0x8a6b24));
    workflow_stop_btn = create_workflow_btn(actions, locale_get("material.stop"),
                                            workflow_stop_cb,
                                            lv_color_hex(0xe94560));

    workflow_update();
    workflow_timer = lv_timer_create(workflow_timer_cb, 500, NULL);

    return workflow_screen;
}

static void material_workflow_destroy(void)
{
    if (workflow_moving)
        workflow_stop_material(0);
    else if (workflow_target_sent)
        workflow_cooldown_nozzle(0);

    if (workflow_timer) {
        lv_timer_delete(workflow_timer);
        workflow_timer = NULL;
    }

    workflow_screen = NULL;
    workflow_status_label = NULL;
    workflow_temp_label = NULL;
    workflow_target_label = NULL;
    workflow_slider = NULL;
    workflow_load_btn = NULL;
    workflow_unload_btn = NULL;
    workflow_stop_btn = NULL;
}

const screen_ops_t screen_material_workflow = {
    .name = "material.change",
    .create = material_workflow_create,
    .destroy = material_workflow_destroy,
    .show_back = true,
};
