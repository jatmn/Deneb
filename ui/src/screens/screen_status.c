/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Status screen - live printer state from native deneb-printsvc. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "print_state_rules.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *status_screen = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *nozzle_temp_label = NULL;
static lv_obj_t *bed_temp_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *file_label = NULL;
static lv_obj_t *pos_label = NULL;
static lv_obj_t *stop_btn = NULL;
static int stop_inflight = 0;

static lv_timer_t *update_timer = NULL;

static void set_btn_enabled(lv_obj_t *btn, int enabled)
{
    if (!btn)
        return;

    if (enabled)
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else
        lv_obj_add_state(btn, LV_STATE_DISABLED);
}

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;

    if (!backend_has_stoppable_print_context() || backend_is_stop_print_inflight())
        return;

    if (stop_inflight)
        return;
    stop_inflight = 1;
    set_btn_enabled(stop_btn, 0);
    if (backend_stop_print() == 0)
        lv_label_set_text(state_label, locale_get("status.cooling"));
    else {
        stop_inflight = 0;
        set_btn_enabled(stop_btn, 1);
    }
}

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

static const char *display_state_locale_key(deneb_print_display_state_t state)
{
    switch (state) {
        case DENEB_PRINT_DISPLAY_STATE_ERROR:
            return "status.error";
        case DENEB_PRINT_DISPLAY_STATE_COOLING:
            return "status.cooling";
        case DENEB_PRINT_DISPLAY_STATE_PAUSED:
            return "status.paused";
        case DENEB_PRINT_DISPLAY_STATE_PREPARING:
            return "status.preparing";
        case DENEB_PRINT_DISPLAY_STATE_PRINTING:
            return "status.printing";
        case DENEB_PRINT_DISPLAY_STATE_IDLE:
        default:
            return "status.idle";
    }
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!status_screen) return;

    const printer_state_t *s = backend_get_state();
    if (!s->connected) {
        lv_label_set_text(state_label, locale_get("status.preparing"));
        lv_label_set_text(nozzle_temp_label, "--- / --- \u00b0C");
        lv_label_set_text(bed_temp_label, "--- / --- \u00b0C");
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(progress_label, "0%");
        lv_label_set_text(file_label, locale_get("status.no_file"));
        lv_label_set_text(pos_label, "X:-- Y:-- Z:--");
        set_btn_enabled(stop_btn, 0);
        return;
    }

    char resolved_name[128];
    backend_get_print_display_name(resolved_name, sizeof(resolved_name));
    int has_print_name = backend_has_print_name(resolved_name);
    int job_active = backend_has_active_print_context();

    if (!job_active && !has_print_name)
        backend_clear_print_display_context_if_idle();

    const char *display_name = locale_get("status.no_file");
    if (job_active && resolved_name[0])
        display_name = resolved_name;

    /* Printer state */
    lv_label_set_text(
        state_label,
        locale_get(display_state_locale_key(deneb_print_display_state(
            s->connected, s->has_error, s->is_paused, s->is_printing,
            backend_has_abort_print_context(),
            backend_has_preparing_print_context(), s->time_total))));

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
    lv_label_set_text(file_label, display_name);

    if (!backend_is_stop_print_inflight())
        stop_inflight = 0;

    if (!job_active && !has_print_name &&
        s->time_total <= 0 &&
        s->time_left <= 0 &&
        s->bed_temp_set <= 0.0f &&
        s->nozzle_temp_set <= 0.0f)
        backend_clear_print_display_context_if_idle();

    set_btn_enabled(stop_btn,
                    backend_has_stoppable_print_context() &&
                    !stop_inflight &&
                    !backend_is_stop_print_inflight());

    /* Position */
    set_position_label(pos_label, s->pos_x, s->pos_y, s->pos_z);

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
    lv_obj_set_style_text_font(name_lbl, &deneb_font_12, 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 24, 0);

    lv_obj_t *temp_lbl = lv_label_create(row);
    lv_label_set_text(temp_lbl, "--- / --- \u00b0C");
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(temp_lbl, &deneb_font_14, 0);
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
    lv_obj_set_style_text_font(state_label, &deneb_font_16, 0);

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
    lv_obj_set_style_text_font(progress_label, &deneb_font_12, 0);
    lv_obj_align(progress_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* File name */
    file_label = lv_label_create(status_screen);
    lv_label_set_text(file_label, locale_get("status.no_file"));
    lv_obj_set_style_text_color(file_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(file_label, &deneb_font_12, 0);
    lv_label_set_long_mode(file_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(file_label, 300);

    /* Stop print button */
    lv_obj_t *stop_row = lv_obj_create(status_screen);
    lv_obj_set_size(stop_row, 300, 36);
    lv_obj_set_style_bg_opa(stop_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stop_row, 0, 0);
    lv_obj_set_style_pad_all(stop_row, 0, 0);
    lv_obj_remove_flag(stop_row, LV_OBJ_FLAG_SCROLLABLE);

    stop_btn = lv_button_create(stop_row);
    lv_obj_set_size(stop_btn, 140, 30);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(stop_btn, 4, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_color(stop_btn, lv_color_hex(0xe0e0e0), 0);
    lv_obj_center(stop_btn);
    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, locale_get("print.stop"));
    lv_obj_set_style_text_font(stop_lbl, &deneb_font_12, 0);
    lv_obj_center(stop_lbl);
    set_btn_enabled(stop_btn, 0);

    /* Position */
    pos_label = lv_label_create(status_screen);
    lv_label_set_text(pos_label, "X:--- Y:--- Z:---");
    lv_obj_set_style_text_color(pos_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(pos_label, &deneb_font_12, 0);

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
    stop_btn = NULL;
}

const screen_ops_t screen_status = {
    .name = "menu.status",
    .create = status_create,
    .destroy = status_destroy,
    .show_back = true,
};
