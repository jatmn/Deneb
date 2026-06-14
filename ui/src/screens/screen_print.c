/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print screen - file browser + print control. LVGL v9.
 * Wired to backend ZMQ IPC.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "print_job_file.h"
#include "print_state_rules.h"
#include "lvgl.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *print_screen = NULL;
static lv_obj_t *file_list = NULL;
static lv_obj_t *status_msg = NULL;
static lv_obj_t *preview_label = NULL;
static lv_obj_t *time_left_label = NULL;
static lv_timer_t *print_timer = NULL;

#define MAX_FILES 32
#define MAX_FILENAME 64
#define MAX_FILE_PATH 128

static char selected_path[MAX_FILE_PATH];
static char selected_name[MAX_FILENAME];

extern const screen_ops_t screen_material;

static char file_names[MAX_FILES][MAX_FILENAME];
static char file_paths[MAX_FILES][MAX_FILE_PATH];
static int file_count = 0;

static void format_duration(int seconds, char *out, size_t out_sz)
{
    int hours;
    int minutes;

    if (!out || out_sz == 0)
        return;
    if (seconds <= 0) {
        snprintf(out, out_sz, "--:--");
        return;
    }

    hours = seconds / 3600;
    minutes = (seconds % 3600) / 60;
    if (hours > 0)
        snprintf(out, out_sz, "%d:%02d", hours, minutes);
    else
        snprintf(out, out_sz, "%d min", minutes > 0 ? minutes : 1);
}

static void update_print_runtime(void)
{
    const printer_state_t *s;
    char duration[16];

    if (!time_left_label)
        return;

    s = backend_get_state();
    if (!s || (!backend_has_active_print_context() && !s->is_paused)) {
        lv_label_set_text(time_left_label, "Left --:--");
        return;
    }

    format_duration(s->time_left, duration, sizeof(duration));
    lv_label_set_text_fmt(time_left_label, "Left %s", duration);
}

static void print_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_print_runtime();
}

static void scan_print_files(const char *path)
{
    file_count = 0;
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (deneb_print_file_is_candidate(ent->d_name)) {
            strncpy(file_names[file_count], ent->d_name, MAX_FILENAME - 1);
            file_names[file_count][MAX_FILENAME - 1] = '\0';
            snprintf(file_paths[file_count], MAX_FILE_PATH, "%s/%s",
                     path, ent->d_name);
            file_count++;
        }
    }
    closedir(dir);
}

static void file_click_cb(lv_event_t *e)
{
    const char *filepath = (const char *)lv_event_get_user_data(e);
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    strncpy(selected_path, filepath, sizeof(selected_path) - 1);
    selected_path[sizeof(selected_path) - 1] = '\0';
    strncpy(selected_name, filename, sizeof(selected_name) - 1);
    selected_name[sizeof(selected_name) - 1] = '\0';

    lv_label_set_text_fmt(preview_label,
                          locale_get("print.selected_fmt"),
                          selected_name);
    const printer_state_t *s = backend_get_state();
    lv_label_set_text(status_msg,
                      s && s->connected ? locale_get("print.ready")
                                        : locale_get("material.busy"));
    fprintf(stderr, "touch-ui: file selected for print: %s (%s)\n", selected_name, selected_path);
}

static void start_btn_cb(lv_event_t *e)
{
    deneb_print_job_start_plan_t plan;

    (void)e;
    if (!selected_path[0]) {
        lv_label_set_text(status_msg, locale_get("print.select_first"));
        return;
    }
    if (!backend_print_start_allowed()) {
        lv_label_set_text(status_msg, locale_get("material.busy"));
        return;
    }

    if (deneb_print_job_start_plan_file(selected_path,
                                        DENEB_PRINT_USB_JOB_SOURCE,
                                        &plan) == 0 &&
        backend_send_job(plan.path, plan.source, plan.uuid, plan.bed_target,
                         plan.nozzle_target) == 0) {
        lv_label_set_text_fmt(status_msg, locale_get("print.starting_fmt"),
                              selected_name);
        fprintf(stderr, "touch-ui: sent JOB command for %s\n", selected_path);
    } else {
        lv_label_set_text(status_msg, locale_get("print.send_failed"));
        fprintf(stderr, "touch-ui: failed to send JOB command for %s\n", selected_path);
    }
}

static void change_material_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_material);
}

static void pause_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_pause_print() == 0)
        lv_label_set_text(status_msg, locale_get("status.paused"));
}

static void resume_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_resume_print() == 0)
        lv_label_set_text(status_msg, locale_get("print.resumed"));
}

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_is_stop_print_inflight())
        return;

    fprintf(stderr, "touch-ui: stop requested\n");
    if (backend_stop_print() == 0)
        lv_label_set_text(status_msg, locale_get("print.stopping"));
    else
        lv_label_set_text(status_msg, locale_get("print.cancelled"));
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    if (file_list) {
        lv_obj_delete(file_list);
        file_list = NULL;
    }

    scan_print_files(DENEB_PRINT_JOB_USB_SCAN_DIR);
    if (file_count == 0)
        scan_print_files(DENEB_PRINT_JOB_LOCAL_SCAN_DIR);

    selected_path[0] = '\0';
    selected_name[0] = '\0';
    lv_label_set_text(preview_label, locale_get("print.select_usb_file"));

    file_list = lv_obj_create(print_screen);
    lv_obj_set_size(file_list, 300, 56);
    lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_radius(file_list, 4, 0);
    lv_obj_set_style_border_color(file_list, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_pad_all(file_list, 4, 0);
    lv_obj_set_flex_flow(file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(file_list, 2, 0);
    lv_obj_set_scroll_dir(file_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(file_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(file_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(file_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    if (file_count == 0) {
        lv_obj_t *empty = lv_label_create(file_list);
        lv_label_set_text(empty, locale_get("print.no_files"));
        lv_obj_set_style_text_color(empty, lv_color_hex(0xa0a0a0), 0);
        return;
    }

    for (int i = 0; i < file_count; i++) {
        lv_obj_t *btn = lv_button_create(file_list);
        lv_obj_set_size(btn, 288, 32);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, file_names[i]);
        lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(lbl, 270);

        lv_obj_add_event_cb(btn, file_click_cb, LV_EVENT_CLICKED, file_paths[i]);
    }
}

static lv_obj_t *print_create(void)
{
    print_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(print_screen, 320, 208);
    lv_obj_align(print_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(print_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(print_screen, 0, 0);
    lv_obj_set_style_border_width(print_screen, 0, 0);
    lv_obj_set_style_pad_all(print_screen, 10, 0);

    /* Refresh button */
    lv_obj_t *refresh_btn = lv_button_create(print_screen);
    lv_obj_set_size(refresh_btn, 88, 32);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x16213e), 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ref_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(ref_lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(ref_lbl);

    preview_label = lv_label_create(print_screen);
    lv_label_set_text(preview_label, locale_get("print.select_usb_file"));
    lv_obj_set_width(preview_label, 202);
    lv_label_set_long_mode(preview_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(preview_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(preview_label, &deneb_font_12, 0);
    lv_obj_align(preview_label, LV_ALIGN_TOP_LEFT, 0, 0);

    time_left_label = lv_label_create(print_screen);
    lv_label_set_text(time_left_label, "Left --:--");
    lv_obj_set_width(time_left_label, 88);
    lv_label_set_long_mode(time_left_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(time_left_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(time_left_label, &deneb_font_12, 0);
    lv_obj_set_style_text_align(time_left_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(time_left_label, LV_ALIGN_TOP_RIGHT, 0, 36);

    /* Control buttons row */
    lv_obj_t *ctrl_row = lv_obj_create(print_screen);
    lv_obj_set_size(ctrl_row, 300, 64);
    lv_obj_align(ctrl_row, LV_ALIGN_BOTTOM_LEFT, 0, -24);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(ctrl_row, 4, 0);
    lv_obj_remove_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *start_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(start_btn, 92, 30);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(start_btn, 4, 0);
    lv_obj_add_event_cb(start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *s_lbl = lv_label_create(start_btn);
    lv_label_set_text(s_lbl, locale_get("print.start"));
    lv_obj_set_style_text_font(s_lbl, &deneb_font_12, 0);
    lv_obj_center(s_lbl);

    lv_obj_t *mat_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(mat_btn, 92, 30);
    lv_obj_set_style_bg_color(mat_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(mat_btn, 4, 0);
    lv_obj_add_event_cb(mat_btn, change_material_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *m_lbl = lv_label_create(mat_btn);
    lv_label_set_text(m_lbl, locale_get("menu.material"));
    lv_obj_set_style_text_font(m_lbl, &deneb_font_12, 0);
    lv_obj_center(m_lbl);

    /* Pause */
    lv_obj_t *pause_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(pause_btn, 92, 30);
    lv_obj_set_style_bg_color(pause_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(pause_btn, 4, 0);
    lv_obj_add_event_cb(pause_btn, pause_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p_lbl = lv_label_create(pause_btn);
    lv_label_set_text(p_lbl, locale_get("print.pause"));
    lv_obj_set_style_text_font(p_lbl, &deneb_font_12, 0);
    lv_obj_center(p_lbl);

    /* Resume */
    lv_obj_t *resume_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(resume_btn, 92, 30);
    lv_obj_set_style_bg_color(resume_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(resume_btn, 4, 0);
    lv_obj_add_event_cb(resume_btn, resume_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *r_lbl = lv_label_create(resume_btn);
    lv_label_set_text(r_lbl, locale_get("print.resume"));
    lv_obj_set_style_text_font(r_lbl, &deneb_font_12, 0);
    lv_obj_center(r_lbl);

    /* Cancel */
    lv_obj_t *stop_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(stop_btn, 92, 30);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(stop_btn, 4, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *c_lbl = lv_label_create(stop_btn);
    lv_label_set_text(c_lbl, locale_get("print.stop"));
    lv_obj_set_style_text_font(c_lbl, &deneb_font_12, 0);
    lv_obj_center(c_lbl);

    /* Status message */
    status_msg = lv_label_create(print_screen);
    lv_label_set_text(status_msg, "");
    lv_obj_set_style_text_color(status_msg, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_msg, &deneb_font_12, 0);
    lv_obj_align(status_msg, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Initial scan */
    refresh_btn_cb(NULL);
    update_print_runtime();
    print_timer = lv_timer_create(print_timer_cb, 500, NULL);

    return print_screen;
}

static void print_destroy(void)
{
    if (print_timer) {
        lv_timer_delete(print_timer);
        print_timer = NULL;
    }
    print_screen = NULL;
    file_list = NULL;
    status_msg = NULL;
    preview_label = NULL;
    time_left_label = NULL;
    selected_path[0] = '\0';
    selected_name[0] = '\0';
}

const screen_ops_t screen_print = {
    .name = "menu.print",
    .create = print_create,
    .destroy = print_destroy,
    .show_back = true,
};
