/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Status screen - live printer state from native deneb-printsvc. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "print_job_file.h"
#include "print_state_rules.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <sys/stat.h>

LV_IMAGE_DECLARE(deneb_logo_116);
LV_IMAGE_DECLARE(ic_nozzle_16);
LV_IMAGE_DECLARE(ic_bed_16);

#define STATUS_THUMB_SIZE 116
#define STATUS_THUMB_STRIDE (STATUS_THUMB_SIZE * 2)
#define STATUS_THUMB_BYTES (STATUS_THUMB_STRIDE * STATUS_THUMB_SIZE)

static lv_obj_t *status_screen = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *nozzle_temp_label = NULL;
static lv_obj_t *bed_temp_label = NULL;
static lv_obj_t *time_left_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *file_label = NULL;
static lv_obj_t *thumb = NULL;
static lv_obj_t *pause_resume_btn = NULL;
static lv_obj_t *pause_resume_label = NULL;
static lv_obj_t *stop_btn = NULL;
static int stop_inflight = 0;
static int thumb_is_job = 0;
static ino_t thumb_inode = 0;
static time_t thumb_mtime = 0;
static off_t thumb_size = 0;
static uint8_t active_thumb_map[STATUS_THUMB_BYTES];
static lv_image_dsc_t active_thumb_dsc = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .flags = 0,
    .w = STATUS_THUMB_SIZE,
    .h = STATUS_THUMB_SIZE,
    .stride = STATUS_THUMB_STRIDE,
    .reserved_2 = 0,
  },
  .data_size = sizeof(active_thumb_map),
  .data = active_thumb_map,
  .reserved = NULL,
};

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
        lv_label_set_text(state_label, locale_get("print.stopping"));
    else {
        stop_inflight = 0;
        set_btn_enabled(stop_btn, 1);
    }
}

static void pause_resume_btn_cb(lv_event_t *e)
{
    (void)e;

    const printer_state_t *s = backend_get_state();
    if (backend_has_abort_print_context())
        return;

    if (s->is_paused) {
        if (backend_resume_print() == 0)
            lv_label_set_text(state_label, locale_get("print.resuming"));
        return;
    }

    if (!backend_has_active_print_context() || !s->is_printing)
        return;

    if (backend_pause_print() == 0)
        lv_label_set_text(state_label, locale_get("print.pausing"));
}

static void set_temp_label(lv_obj_t *label, float cur, float target)
{
    char text[32];
    snprintf(text, sizeof(text), "%.0f / %.0f \u00b0C", cur, target);
    lv_label_set_text(label, text);
}

static void format_duration(int seconds, char *out, size_t out_sz)
{
    int hours, minutes;

    if (seconds < 0)
        seconds = 0;

    hours = seconds / 3600;
    minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    snprintf(out, out_sz, "%d:%02d:%02d", hours, minutes, seconds);
}

static void set_time_left_label(lv_obj_t *label, const printer_state_t *s)
{
    char duration[16];

    if (!label)
        return;

    if (!s || !backend_has_active_print_context() || s->time_left <= 0) {
        lv_label_set_text(label, "Left --:--");
        return;
    }

    format_duration(s->time_left, duration, sizeof(duration));
    lv_label_set_text_fmt(label, "Left %s", duration);
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
        case DENEB_PRINT_DISPLAY_STATE_PAUSING:
            return "status.pausing";
        case DENEB_PRINT_DISPLAY_STATE_PREPARING:
            return "status.preparing";
        case DENEB_PRINT_DISPLAY_STATE_PRINTING:
            return "status.printing";
        case DENEB_PRINT_DISPLAY_STATE_IDLE:
        default:
            return "status.idle";
    }
}

static void set_thumb_source(int job_active)
{
    const void *src;
    int want_job;
    struct stat st;
    FILE *f;

    if (!thumb)
        return;

    want_job = job_active &&
               stat(DENEB_ACTIVE_THUMB_PATH, &st) == 0 &&
               st.st_size == (off_t)sizeof(active_thumb_map);

    if (want_job == thumb_is_job &&
        (!want_job || (st.st_ino == thumb_inode &&
                       st.st_mtime == thumb_mtime &&
                       st.st_size == thumb_size)))
        return;

    if (want_job) {
        f = fopen(DENEB_ACTIVE_THUMB_PATH, "rb");
        if (!f ||
            fread(active_thumb_map, 1, sizeof(active_thumb_map), f) !=
                sizeof(active_thumb_map)) {
            if (f)
                fclose(f);
            want_job = 0;
        } else
            fclose(f);
    }

    src = want_job ? (const void *)&active_thumb_dsc
                   : (const void *)&deneb_logo_116;
    thumb_is_job = want_job;
    thumb_inode = want_job ? st.st_ino : 0;
    thumb_mtime = want_job ? st.st_mtime : 0;
    thumb_size = want_job ? st.st_size : 0;
    lv_image_cache_drop(NULL);
    lv_image_set_src(thumb, src);
    lv_obj_invalidate(thumb);
}

static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!status_screen) return;

    const printer_state_t *s = backend_get_state();
    if (!s->connected) {
        lv_label_set_text(state_label, locale_get("status.preparing"));
        lv_label_set_text(nozzle_temp_label, "-- / --");
        lv_label_set_text(bed_temp_label, "-- / --");
        lv_label_set_text(time_left_label, "Left --:--");
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(progress_label, "0%");
        lv_label_set_text(file_label, locale_get("status.no_file"));
        set_btn_enabled(pause_resume_btn, 0);
        if (pause_resume_label)
            lv_label_set_text(pause_resume_label, locale_get("print.pause"));
        set_btn_enabled(stop_btn, 0);
        set_thumb_source(0);
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
        locale_get(display_state_locale_key(deneb_print_display_state_with_req(
            s->connected, s->has_error, s->is_paused, s->is_printing,
            backend_has_abort_print_context(),
            backend_has_preparing_print_context(), s->time_total,
            s->current_req))));

    /* Temperatures */
    set_temp_label(nozzle_temp_label, s->nozzle_temp_cur, s->nozzle_temp_set);
    set_temp_label(bed_temp_label, s->bed_temp_cur, s->bed_temp_set);

    /* Time remaining */
    set_time_left_label(time_left_label, s);

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

    int aborting = backend_has_abort_print_context();
    int pausing = deneb_print_display_state_with_req(
        s->connected, s->has_error, s->is_paused, s->is_printing,
        aborting, backend_has_preparing_print_context(), s->time_total,
        s->current_req) == DENEB_PRINT_DISPLAY_STATE_PAUSING;
    if (pause_resume_label)
        lv_label_set_text(pause_resume_label,
                          locale_get(s->is_paused ? "print.resume" :
                                                   "print.pause"));
    set_btn_enabled(pause_resume_btn,
                    ((job_active && s->is_printing) || s->is_paused) &&
                    !aborting && !pausing);
    set_btn_enabled(stop_btn, backend_has_stoppable_print_context() &&
                                  !stop_inflight &&
                                  !backend_is_stop_print_inflight());

    /* Thumbnail */
    set_thumb_source(job_active);
}

static lv_obj_t *create_temp_row(lv_obj_t *parent, const lv_image_dsc_t *icon)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 108, 24);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon_img = lv_image_create(row);
    lv_image_set_src(icon_img, icon);
    lv_obj_align(icon_img, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *temp_lbl = lv_label_create(row);
    lv_label_set_text(temp_lbl, "-- / --");
    lv_obj_set_width(temp_lbl, 80);
    lv_obj_set_style_text_align(temp_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(temp_lbl, &deneb_font_14, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 28, 0);

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
    lv_obj_set_style_pad_all(status_screen, 4, 0);
    lv_obj_set_flex_flow(status_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(status_screen, 3, 0);

    /* Top row: thumbnail + info */
    lv_obj_t *top_row = lv_obj_create(status_screen);
    lv_obj_set_size(top_row, 312, 116);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_gap(top_row, 4, 0);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_remove_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

    thumb = lv_image_create(top_row);
    lv_image_set_src(thumb, &deneb_logo_116);
    lv_obj_set_size(thumb, 116, 116);
    lv_image_set_inner_align(thumb, LV_IMAGE_ALIGN_CONTAIN);
    thumb_is_job = 0;

    lv_obj_t *info_col = lv_obj_create(top_row);
    lv_obj_set_size(info_col, 192, 116);
    lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(info_col, 0, 0);
    lv_obj_set_style_pad_gap(info_col, 2, 0);
    lv_obj_set_style_bg_opa(info_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_col, 0, 0);
    lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(info_col, LV_OBJ_FLAG_SCROLLABLE);

    /* State */
    state_label = lv_label_create(info_col);
    lv_label_set_text(state_label, locale_get("status.idle"));
    lv_obj_set_width(state_label, 192);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(state_label, &deneb_font_16, 0);

    /* Temps */
    nozzle_temp_label = create_temp_row(info_col, &ic_nozzle_16);
    bed_temp_label = create_temp_row(info_col, &ic_bed_16);

    /* Time remaining */
    time_left_label = lv_label_create(info_col);
    lv_label_set_text(time_left_label, "Left --:--");
    lv_obj_set_width(time_left_label, 192);
    lv_label_set_long_mode(time_left_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(time_left_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(time_left_label, &deneb_font_12, 0);
    lv_obj_set_style_text_align(time_left_label, LV_TEXT_ALIGN_RIGHT, 0);

    /* File name */
    file_label = lv_label_create(status_screen);
    lv_label_set_text(file_label, locale_get("status.no_file"));
    lv_obj_set_width(file_label, 312);
    lv_obj_set_style_text_color(file_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(file_label, &deneb_font_12, 0);
    lv_label_set_long_mode(file_label, LV_LABEL_LONG_MODE_DOTS);

    /* Progress bar */
    lv_obj_t *prog_row = lv_obj_create(status_screen);
    lv_obj_set_size(prog_row, 312, 18);
    lv_obj_set_style_bg_opa(prog_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prog_row, 0, 0);
    lv_obj_set_style_pad_all(prog_row, 0, 0);
    lv_obj_remove_flag(prog_row, LV_OBJ_FLAG_SCROLLABLE);

    progress_bar = lv_bar_create(prog_row);
    lv_obj_set_size(progress_bar, 262, 12);
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
    lv_obj_set_width(progress_label, 44);
    lv_label_set_long_mode(progress_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(progress_label, LV_ALIGN_RIGHT_MID, 0, 0);

    /* Controls */
    lv_obj_t *ctrl_row = lv_obj_create(status_screen);
    lv_obj_set_size(ctrl_row, 312, 36);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    pause_resume_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(pause_resume_btn, 150, 36);
    lv_obj_set_style_bg_color(pause_resume_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(pause_resume_btn, 4, 0);
    lv_obj_add_event_cb(pause_resume_btn, pause_resume_btn_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_color(pause_resume_btn, lv_color_hex(0xe0e0e0), 0);
    pause_resume_label = lv_label_create(pause_resume_btn);
    lv_label_set_text(pause_resume_label, locale_get("print.pause"));
    lv_obj_set_style_text_font(pause_resume_label, &deneb_font_12, 0);
    lv_obj_center(pause_resume_label);
    set_btn_enabled(pause_resume_btn, 0);

    stop_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(stop_btn, 150, 36);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(stop_btn, 4, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_color(stop_btn, lv_color_hex(0xe0e0e0), 0);
    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, locale_get("print.stop"));
    lv_obj_set_style_text_font(stop_lbl, &deneb_font_12, 0);
    lv_obj_center(stop_lbl);
    set_btn_enabled(stop_btn, 0);

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
    time_left_label = NULL;
    progress_bar = NULL;
    progress_label = NULL;
    file_label = NULL;
    thumb = NULL;
    pause_resume_btn = NULL;
    pause_resume_label = NULL;
    stop_btn = NULL;
    stop_inflight = 0;
    thumb_is_job = 0;
    thumb_inode = 0;
    thumb_mtime = 0;
    thumb_size = 0;
}

const screen_ops_t screen_status = {
    .name = "menu.status",
    .create = status_create,
    .destroy = status_destroy,
    .show_back = true,
};
