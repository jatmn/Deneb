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
#include <ctype.h>

#define DENEB_CLUSTER_PENDING_JOB "/tmp/deneb-cluster-print-job.json"

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
static char active_print_name[128];

static lv_timer_t *update_timer = NULL;

static int is_print_lifecycle_filename(const char *name)
{
    if (!name || !*name || strcmp(name, "none") == 0)
        return 0;

    if (strstr(name, "home_and_center_head") != NULL)
        return 1;
    if (strstr(name, "move_buildplate_up") != NULL)
        return 1;
    if (strstr(name, "move_buildplate_down") != NULL)
        return 1;
    if (strstr(name, "macro") != NULL && strstr(name, ".gcode") != NULL)
        return 1;

    return 0;
}

static int req_equals_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;

    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb)
            return 0;
    }

    return *a == '\0' && *b == '\0';
}

static int str_is_one_of_ci(const char *value, const char *const *choices)
{
    if (!value || !*value)
        return 0;

    for (int i = 0; choices[i]; i++) {
        if (req_equals_ci(value, choices[i]))
            return 1;
    }

    return 0;
}

static int is_print_lifecycle_req(const char *req)
{
    static const char *const lifecycle_reqs[] = {
        "HOME", "HOMING", "HOME_AND_CENTER_HEAD",
        "RESOLVE_CONFLICTS", "PREPARE", "PREHEAT", "PREHEATING",
        "BED_PREHEATING", "HEAT_BED", "BED_AND_NOZZLE_PREHEATING",
        "EXTRACT", "EXTRACTING",
        NULL
    };

    return str_is_one_of_ci(req, lifecycle_reqs);
}

static int is_stoppable_print_req(const char *req)
{
    static const char *const stoppable_reqs[] = {
        "JOB", "Print", "Printing",
        "PAUSE", "Pause", "Paused",
        NULL
    };

    return str_is_one_of_ci(req, stoppable_reqs);
}

static int is_idle_like_req(const char *req)
{
    static const char *const idle_reqs[] = {
        "", "IDLE", "READY", "Idle", "Ready", "Finished", "STOPPED", NULL
    };

    if (!req || !*req)
        return 1;

    return str_is_one_of_ci(req, idle_reqs);
}

static int is_print_job_filename(const char *name)
{
    if (!name || !*name || strcmp(name, "none") == 0)
        return 0;

    if (is_print_lifecycle_filename(name))
        return 0;
    if (strstr(name, ".gcode") == NULL && strstr(name, ".ufp") == NULL)
        return 0;

    return 1;
}

static int read_cluster_pending_field(const char *field, char *out, size_t out_sz)
{
    if (!out_sz)
        return -1;

    FILE *f = fopen(DENEB_CLUSTER_PENDING_JOB, "rb");
    if (!f)
        return -1;

    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0)
        return -1;

    buf[n] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", field);

    const char *p = strstr(buf, needle);
    if (!p)
        return -1;

    p = strchr(p + strlen(needle), ':');
    if (!p)
        return -1;
    p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '"' && *p != '\'')
        return -1;

    char quote = *p++;
    size_t i = 0;
    while (*p && *p != quote && i < out_sz - 1) {
        if (*p == '\\' && p[1])
            p++;
        out[i++] = *p++;
    }
    if (*p != quote)
        return -1;

    out[i] = '\0';
    return 0;
}

static int read_cluster_pending_name(char *out, size_t out_sz)
{
    char value[256];

    if (read_cluster_pending_field("name", value, sizeof(value)) == 0) {
        if (value[0] && strcmp(value, "none") != 0) {
            strncpy(out, value, out_sz - 1);
            out[out_sz - 1] = '\0';
            return 0;
        }
    }

    if (read_cluster_pending_field("path", value, sizeof(value)) == 0) {
        if (value[0] && strcmp(value, "none") != 0) {
            const char *base = strrchr(value, '/');
            base = base ? base + 1 : value;
            if (base && *base && strcmp(base, "none") != 0) {
                strncpy(out, base, out_sz - 1);
                out[out_sz - 1] = '\0';
                return 0;
            }
        }
    }

    return -1;
}

static int state_has_print_name(const printer_state_t *s)
{
    if (is_print_job_filename(s->filename))
        return 1;

    char pending_name[128];
    return read_cluster_pending_name(pending_name, sizeof(pending_name)) == 0 &&
           is_print_job_filename(pending_name);
}

static int has_active_print_context(const printer_state_t *s, int has_print_name)
{
    return s->is_printing ||
           s->is_paused ||
           s->time_total > 0 ||
           s->time_left > 0 ||
           (!is_idle_like_req(s->current_req) && s->current_req[0] != '\0') ||
           has_print_name ||
           ((s->bed_temp_set > 0.0f || s->nozzle_temp_set > 0.0f) &&
            !is_idle_like_req(s->current_req));
}

static int has_heat_targets(const printer_state_t *s)
{
    return s->bed_temp_set > 0.0f || s->nozzle_temp_set > 0.0f;
}

static int has_preparing_print_context(const printer_state_t *s, int has_print_name)
{
    return has_print_name &&
           (is_print_lifecycle_req(s->current_req) ||
            has_heat_targets(s) ||
            (!is_idle_like_req(s->current_req) && s->current_req[0] != '\0'));
}

static int has_stoppable_print_context(const printer_state_t *s, int has_print_name)
{
    if (s->is_paused)
        return 1;

    if (!has_print_name)
        return 0;

    if (s->is_printing || s->time_total > 0 || s->time_left > 0)
        return 1;

    return is_stoppable_print_req(s->current_req) ||
           is_print_lifecycle_req(s->current_req) ||
           has_heat_targets(s);
}

static void clear_active_print_context_if_idle(const printer_state_t *s, int has_print_name)
{
    if (!has_active_print_context(s, has_print_name) &&
        s->time_total <= 0 &&
        s->time_left <= 0 &&
        s->bed_temp_set == 0.0f &&
        s->nozzle_temp_set == 0.0f) {
        active_print_name[0] = '\0';
    }
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

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;
    const printer_state_t *s = backend_get_state();
    if (!has_stoppable_print_context(s, state_has_print_name(s)) ||
        backend_is_stop_print_inflight())
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

    int has_print_name = 0;
    const char *raw_name = s->filename[0] && strcmp(s->filename, "none") != 0 ? s->filename : "";
    char pending_name[128];
    if ((!raw_name[0] || is_print_lifecycle_filename(raw_name)) &&
        read_cluster_pending_name(pending_name, sizeof(pending_name)) == 0 &&
        !is_print_lifecycle_filename(pending_name)) {
        raw_name = pending_name;
    }

    has_print_name = is_print_job_filename(raw_name);

    if (raw_name[0] && !is_print_lifecycle_filename(raw_name)) {
        strncpy(active_print_name, raw_name, sizeof(active_print_name) - 1);
        active_print_name[sizeof(active_print_name) - 1] = '\0';
    }
    int job_active = has_active_print_context(s, has_print_name);

    if (!job_active && !has_print_name)
        clear_active_print_context_if_idle(s, has_print_name);

    const char *display_name = locale_get("status.no_file");
    if (job_active) {
        if (active_print_name[0])
            display_name = active_print_name;
        else if (raw_name[0] && !is_print_lifecycle_filename(raw_name))
            display_name = raw_name;
    }

    /* Printer state */
    if (s->is_paused)
        lv_label_set_text(state_label, locale_get("status.paused"));
    else if (has_preparing_print_context(s, has_print_name) && !s->time_total)
        lv_label_set_text(state_label, locale_get("status.preparing"));
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
    lv_label_set_text(file_label, display_name);

    if (!backend_is_stop_print_inflight())
        stop_inflight = 0;

    if (!job_active && !has_print_name &&
        s->time_total <= 0 &&
        s->time_left <= 0 &&
        s->bed_temp_set <= 0.0f &&
        s->nozzle_temp_set <= 0.0f)
        clear_active_print_context_if_idle(s, has_print_name);

    set_btn_enabled(stop_btn,
                    has_stoppable_print_context(s, has_print_name) &&
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
    active_print_name[0] = '\0';

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
    active_print_name[0] = '\0';
}

const screen_ops_t screen_status = {
    .name = "menu.status",
    .create = status_create,
    .destroy = status_destroy,
    .show_back = true,
};
