/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Status screen - live printer state from coordinator. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *status_screen = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *nozzle_temp_label = NULL;
static lv_obj_t *bed_temp_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *file_label = NULL;
static lv_obj_t *pos_label = NULL;

static lv_timer_t *update_timer = NULL;

static void set_temp_label(lv_obj_t *label, float cur, float target)
{
    char text[32];
    snprintf(text, sizeof(text), "%.0f / %.0f \u00b0C", cur, target);
    lv_label_set_text(label, text);
}

static void set_position_label(lv_obj_t *label, float x, float y, float z)
{
    char text[40];
    snprintf(text, sizeof(text), "X:%.0f Y:%.0f Z:%.1f", x, y, z);
    lv_label_set_text(label, text);
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!status_screen) return;

    const printer_state_t *s = backend_get_state();

    /* Printer state */
    if (s->is_paused)
        lv_label_set_text(state_label, locale_get("status.paused"));
    else if (s->is_printing)
        lv_label_set_text(state_label, locale_get("status.printing"));
    else
        lv_label_set_text(state_label, locale_get("status.idle"));

    /* Temperatures */
    set_temp_label(nozzle_temp_label, s->nozzle_temp_cur, s->nozzle_temp_set);
    set_temp_label(bed_temp_label, s->bed_temp_cur, s->bed_temp_set);

    /* Progress */
    int pct = (int)s->progress;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(progress_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(progress_label, "%d%%", pct);

    /* File */
    if (s->filename[0] && strcmp(s->filename, "none") != 0)
        lv_label_set_text(file_label, s->filename);
    else
        lv_label_set_text(file_label, locale_get("status.no_file"));

    /* Position */
    set_position_label(pos_label, s->pos_x, s->pos_y, s->pos_z);

    /* Connection indicator */
    if (!s->connected) {
        lv_label_set_text(state_label, "---");
    }
}

static lv_obj_t *create_temp_row(lv_obj_t *parent, const char *icon,
                                 const char *label_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 300, 28);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_lbl = lv_label_create(row);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xe94560), 0);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *name_lbl = lv_label_create(row);
    lv_label_set_text(name_lbl, label_text);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    lv_obj_t *temp_lbl = lv_label_create(row);
    lv_label_set_text(temp_lbl, "--- / --- \u00b0C");
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    return temp_lbl;
}

static lv_obj_t *status_create(void)
{
    status_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(status_screen, 320, 208);
    lv_obj_align(status_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(status_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(status_screen, 0, 0);
    lv_obj_set_style_border_width(status_screen, 0, 0);
    lv_obj_set_style_pad_all(status_screen, 10, 0);
    lv_obj_set_flex_flow(status_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(status_screen, 6, 0);

    /* State */
    state_label = lv_label_create(status_screen);
    lv_label_set_text(state_label, locale_get("status.idle"));
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_16, 0);

    /* Temperatures */
    nozzle_temp_label = create_temp_row(status_screen, LV_SYMBOL_WARNING,
                                        locale_get("status.nozzle"));
    bed_temp_label = create_temp_row(status_screen, LV_SYMBOL_WARNING,
                                     locale_get("status.bed"));

    /* Progress bar */
    lv_obj_t *prog_row = lv_obj_create(status_screen);
    lv_obj_set_size(prog_row, 300, 20);
    lv_obj_set_style_bg_opa(prog_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prog_row, 0, 0);
    lv_obj_set_style_pad_all(prog_row, 0, 0);
    lv_obj_remove_flag(prog_row, LV_OBJ_FLAG_SCROLLABLE);

    progress_bar = lv_bar_create(prog_row);
    lv_obj_set_size(progress_bar, 240, 14);
    lv_obj_align(progress_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x53a8b6),
                              LV_PART_INDICATOR);

    progress_label = lv_label_create(prog_row);
    lv_label_set_text(progress_label, "0%");
    lv_obj_set_style_text_color(progress_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_12, 0);
    lv_obj_align(progress_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* File name */
    file_label = lv_label_create(status_screen);
    lv_label_set_text(file_label, locale_get("status.no_file"));
    lv_obj_set_style_text_color(file_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(file_label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(file_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(file_label, 300);

    /* Position */
    pos_label = lv_label_create(status_screen);
    lv_label_set_text(pos_label, "X:--- Y:--- Z:---");
    lv_obj_set_style_text_color(pos_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(pos_label, &lv_font_montserrat_12, 0);

    /* Start live update timer (every 250ms) */
    update_timer = lv_timer_create(update_timer_cb, 250, NULL);

    return status_screen;
}

static void status_destroy(void)
{
    if (update_timer) {
        lv_timer_delete(update_timer);
        update_timer = NULL;
    }
    status_screen = NULL;
    state_label = NULL;
    nozzle_temp_label = NULL;
    bed_temp_label = NULL;
    progress_bar = NULL;
    progress_label = NULL;
    file_label = NULL;
    pos_label = NULL;
}

const screen_ops_t screen_status = {
    .name = "Status",
    .create = status_create,
    .destroy = status_destroy,
    .show_back = true,
};
