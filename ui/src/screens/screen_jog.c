/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Manual jog screen - uses coordinator macros for homing. LVGL v9.
 *
 * Macro files on device:
 *   home_and_center_head.gcode - Home all axes and center head
 *   home_release.gcode         - Home and release motors
 *   move_buildplate_up.gcode   - Move buildplate up
 *   move_buildplate_down.gcode - Move buildplate down
 *
 * Direct GCODE for jogging:
 *   G91 (relative), G1 X10 F3000 (move), G90 (absolute)
 *   G28 X Y (home XY), G28 Z (home Z)
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *jog_screen = NULL;
static lv_obj_t *x_pos_label = NULL;
static lv_obj_t *y_pos_label = NULL;
static lv_obj_t *z_pos_label = NULL;
static lv_obj_t *step_label = NULL;
static lv_timer_t *pos_timer = NULL;

static const int step_sizes[] = {1, 10, 50};
static int current_step_idx = 1;

static int motion_allowed(void)
{
    const printer_state_t *s = backend_get_state();
    return s && s->connected && !s->is_printing && !s->is_paused &&
           !s->has_error;
}

static void set_position_label(lv_obj_t *label, const char *axis, float pos)
{
    char text[16];
    snprintf(text, sizeof(text), "%s:%.1f", axis, pos);
    lv_label_set_text(label, text);
}

static void update_step_label(void)
{
    if (step_label)
        lv_label_set_text_fmt(step_label, "%d mm", step_sizes[current_step_idx]);
}

static void pos_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const printer_state_t *s = backend_get_state();
    if (x_pos_label)
        set_position_label(x_pos_label, "X", s->pos_x);
    if (y_pos_label)
        set_position_label(y_pos_label, "Y", s->pos_y);
    if (z_pos_label)
        set_position_label(z_pos_label, "Z", s->pos_z);
}

static void jog_btn_cb(lv_event_t *e)
{
    if (!motion_allowed())
        return;

    const char *axis_dir = (const char *)lv_event_get_user_data(e);
    int step = step_sizes[current_step_idx];
    char gcode[64];
    char axis = axis_dir[0];
    int sign = (axis_dir[1] == '+') ? 1 : -1;

    /* Send as single GCODE command: G91, G1, G90 */
    snprintf(gcode, sizeof(gcode), "G91");
    backend_send_gcode(gcode);
    snprintf(gcode, sizeof(gcode), "G1 %c%d F3000", axis, step * sign);
    backend_send_gcode(gcode);
    snprintf(gcode, sizeof(gcode), "G90");
    backend_send_gcode(gcode);
}

static void home_xy_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!motion_allowed())
        return;
    backend_send_command("MACRO", "{\"macro\":\"home_and_center_head.gcode\"}");
}

static void home_z_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!motion_allowed())
        return;
    backend_send_gcode("G28 Z");
}

static void step_btn_cb(lv_event_t *e)
{
    (void)e;
    current_step_idx = (current_step_idx + 1) % 3;
    update_step_label();
}

static void bed_up_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!motion_allowed())
        return;
    backend_send_command("MACRO", "{\"macro\":\"move_buildplate_up.gcode\"}");
}

static void bed_down_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!motion_allowed())
        return;
    backend_send_command("MACRO", "{\"macro\":\"move_buildplate_down.gcode\"}");
}

static lv_obj_t *create_jog_btn(lv_obj_t *parent, const char *symbol,
                                const char *user_data, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *jog_create(void)
{
    jog_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(jog_screen, 320, 208);
    lv_obj_align(jog_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(jog_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(jog_screen, 0, 0);
    lv_obj_set_style_border_width(jog_screen, 0, 0);
    lv_obj_set_style_pad_all(jog_screen, 8, 0);

    /* XY D-pad */
    create_jog_btn(jog_screen, LV_SYMBOL_UP, "Y+", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 56, 4);

    create_jog_btn(jog_screen, LV_SYMBOL_LEFT, "X-", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 0, 44);

    /* Home XY (center) */
    lv_obj_t *home_btn = lv_button_create(jog_screen);
    lv_obj_set_size(home_btn, 56, 36);
    lv_obj_set_style_bg_color(home_btn, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_radius(home_btn, 4, 0);
    lv_obj_add_event_cb(home_btn, home_xy_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_lbl = lv_label_create(home_btn);
    lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
    lv_obj_center(home_lbl);
    lv_obj_align(home_btn, LV_ALIGN_TOP_LEFT, 56, 44);

    create_jog_btn(jog_screen, LV_SYMBOL_RIGHT, "X+", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 112, 44);

    create_jog_btn(jog_screen, LV_SYMBOL_DOWN, "Y-", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 56, 84);

    /* Step size */
    lv_obj_t *step_btn = lv_button_create(jog_screen);
    lv_obj_set_size(step_btn, 80, 28);
    lv_obj_set_style_bg_color(step_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(step_btn, 4, 0);
    lv_obj_add_event_cb(step_btn, step_btn_cb, LV_EVENT_CLICKED, NULL);
    step_label = lv_label_create(step_btn);
    lv_obj_center(step_label);
    update_step_label();
    lv_obj_align(step_btn, LV_ALIGN_TOP_LEFT, 0, 130);

    /* Right panel: Z + Bed */
    lv_obj_t *z_title = lv_label_create(jog_screen);
    lv_label_set_text(z_title, "Z");
    lv_obj_set_style_text_color(z_title, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(z_title, &lv_font_montserrat_14, 0);
    lv_obj_align(z_title, LV_ALIGN_TOP_LEFT, 190, 8);

    create_jog_btn(jog_screen, LV_SYMBOL_UP, "Z+", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 190, 28);

    create_jog_btn(jog_screen, LV_SYMBOL_DOWN, "Z-", jog_btn_cb);
    lv_obj_align(lv_obj_get_child(jog_screen, -1), LV_ALIGN_TOP_LEFT, 190, 68);

    /* Home Z */
    lv_obj_t *home_z_btn = lv_button_create(jog_screen);
    lv_obj_set_size(home_z_btn, 56, 24);
    lv_obj_set_style_bg_color(home_z_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(home_z_btn, 4, 0);
    lv_obj_add_event_cb(home_z_btn, home_z_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hz_lbl = lv_label_create(home_z_btn);
    lv_label_set_text(hz_lbl, "Z Home");
    lv_obj_set_style_text_font(hz_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(hz_lbl);
    lv_obj_align(home_z_btn, LV_ALIGN_TOP_LEFT, 250, 28);

    /* Bed up/down */
    lv_obj_t *bed_title = lv_label_create(jog_screen);
    lv_label_set_text(bed_title, "Bed");
    lv_obj_set_style_text_color(bed_title, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(bed_title, &lv_font_montserrat_14, 0);
    lv_obj_align(bed_title, LV_ALIGN_TOP_LEFT, 250, 58);

    lv_obj_t *bed_up_btn = lv_button_create(jog_screen);
    lv_obj_set_size(bed_up_btn, 56, 24);
    lv_obj_set_style_bg_color(bed_up_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(bed_up_btn, 4, 0);
    lv_obj_add_event_cb(bed_up_btn, bed_up_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bu_lbl = lv_label_create(bed_up_btn);
    lv_label_set_text(bu_lbl, LV_SYMBOL_UP);
    lv_obj_center(bu_lbl);
    lv_obj_align(bed_up_btn, LV_ALIGN_TOP_LEFT, 250, 76);

    lv_obj_t *bed_down_btn = lv_button_create(jog_screen);
    lv_obj_set_size(bed_down_btn, 56, 24);
    lv_obj_set_style_bg_color(bed_down_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(bed_down_btn, 4, 0);
    lv_obj_add_event_cb(bed_down_btn, bed_down_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bd_lbl = lv_label_create(bed_down_btn);
    lv_label_set_text(bd_lbl, LV_SYMBOL_DOWN);
    lv_obj_center(bd_lbl);
    lv_obj_align(bed_down_btn, LV_ALIGN_TOP_LEFT, 250, 104);

    /* Position display */
    lv_obj_t *pos_row = lv_obj_create(jog_screen);
    lv_obj_set_size(pos_row, 300, 20);
    lv_obj_align(pos_row, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(pos_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pos_row, 0, 0);
    lv_obj_set_style_pad_all(pos_row, 0, 0);

    x_pos_label = lv_label_create(pos_row);
    lv_label_set_text(x_pos_label, "X:---");
    lv_obj_set_style_text_color(x_pos_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(x_pos_label, &lv_font_montserrat_12, 0);
    lv_obj_align(x_pos_label, LV_ALIGN_LEFT_MID, 0, 0);

    y_pos_label = lv_label_create(pos_row);
    lv_label_set_text(y_pos_label, "Y:---");
    lv_obj_set_style_text_color(y_pos_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(y_pos_label, &lv_font_montserrat_12, 0);
    lv_obj_align(y_pos_label, LV_ALIGN_CENTER, 0, 0);

    z_pos_label = lv_label_create(pos_row);
    lv_label_set_text(z_pos_label, "Z:---");
    lv_obj_set_style_text_color(z_pos_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(z_pos_label, &lv_font_montserrat_12, 0);
    lv_obj_align(z_pos_label, LV_ALIGN_RIGHT_MID, 0, 0);

    pos_timer = lv_timer_create(pos_timer_cb, 250, NULL);

    return jog_screen;
}

static void jog_destroy(void)
{
    if (pos_timer) {
        lv_timer_delete(pos_timer);
        pos_timer = NULL;
    }
    jog_screen = NULL;
    x_pos_label = NULL;
    y_pos_label = NULL;
    z_pos_label = NULL;
    step_label = NULL;
}

const screen_ops_t screen_jog = {
    .name = "Manual Control",
    .create = jog_create,
    .destroy = jog_destroy,
    .show_back = true,
};
