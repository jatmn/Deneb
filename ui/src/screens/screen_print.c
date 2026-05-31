/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print screen - file browser + print control. LVGL v9.
 * Wired to backend ZMQ IPC.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "lvgl.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *print_screen = NULL;
static lv_obj_t *file_list = NULL;
static lv_obj_t *status_msg = NULL;

#define MAX_FILES 32
#define MAX_FILENAME 64
#define MAX_FILE_PATH 128

static char file_names[MAX_FILES][MAX_FILENAME];
static char file_paths[MAX_FILES][MAX_FILE_PATH];
static int file_count = 0;

static void json_escape_string(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t out = 0;

    if (dst_size == 0)
        return;

    while (*src && out + 1 < dst_size) {
        unsigned char c = (unsigned char)*src++;

        if (c == '"' || c == '\\') {
            if (out + 2 >= dst_size)
                break;
            dst[out++] = '\\';
            dst[out++] = (char)c;
        } else if (c < 0x20) {
            if (out + 6 >= dst_size)
                break;
            dst[out++] = '\\';
            dst[out++] = 'u';
            dst[out++] = '0';
            dst[out++] = '0';
            dst[out++] = hex[c >> 4];
            dst[out++] = hex[c & 0x0f];
        } else {
            dst[out++] = (char)c;
        }
    }

    dst[out] = '\0';
}

static void scan_print_files(const char *path)
{
    file_count = 0;
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && file_count < MAX_FILES) {
        size_t len = strlen(ent->d_name);
        int is_gcode = (len > 6 && strcmp(ent->d_name + len - 6, ".gcode") == 0);
        int is_ufp = (len > 4 && strcmp(ent->d_name + len - 4, ".ufp") == 0);
        if (is_gcode || is_ufp) {
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

    /* Send JOB command to coordinator with full path */
    char escaped_path[MAX_FILE_PATH * 2 + 1];
    char args[384];
    json_escape_string(filepath, escaped_path, sizeof(escaped_path));
    snprintf(args, sizeof(args), "{\"file\":\"%s\",\"source\":\"USB\",\"uuid\":\"0\"}", escaped_path);
    if (backend_send_command("JOB", args) == 0) {
        lv_label_set_text_fmt(status_msg, "Starting: %s", filename);
    } else {
        lv_label_set_text(status_msg, "Error: send failed");
    }
}

static void pause_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_pause_print() == 0)
        lv_label_set_text(status_msg, "Paused");
}

static void resume_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_resume_print() == 0)
        lv_label_set_text(status_msg, "Resumed");
}

static void cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    if (backend_abort_print() == 0)
        lv_label_set_text(status_msg, "Cancelled");
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    if (file_list) {
        lv_obj_delete(file_list);
        file_list = NULL;
    }

    scan_print_files("/mnt/sda1");
    if (file_count == 0)
        scan_print_files("/home/3D");

    file_list = lv_obj_create(print_screen);
    lv_obj_set_size(file_list, 300, 112);
    lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 30);
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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
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

    /* Control buttons row */
    lv_obj_t *ctrl_row = lv_obj_create(print_screen);
    lv_obj_set_size(ctrl_row, 300, 36);
    lv_obj_align(ctrl_row, LV_ALIGN_BOTTOM_LEFT, 0, -24);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(ctrl_row, 4, 0);
    lv_obj_remove_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Pause */
    lv_obj_t *pause_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(pause_btn, 92, 32);
    lv_obj_set_style_bg_color(pause_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(pause_btn, 4, 0);
    lv_obj_add_event_cb(pause_btn, pause_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *p_lbl = lv_label_create(pause_btn);
    lv_label_set_text(p_lbl, locale_get("print.pause"));
    lv_obj_set_style_text_font(p_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(p_lbl);

    /* Resume */
    lv_obj_t *resume_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(resume_btn, 92, 32);
    lv_obj_set_style_bg_color(resume_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(resume_btn, 4, 0);
    lv_obj_add_event_cb(resume_btn, resume_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *r_lbl = lv_label_create(resume_btn);
    lv_label_set_text(r_lbl, locale_get("print.resume"));
    lv_obj_set_style_text_font(r_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(r_lbl);

    /* Cancel */
    lv_obj_t *cancel_btn = lv_button_create(ctrl_row);
    lv_obj_set_size(cancel_btn, 92, 32);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_radius(cancel_btn, 4, 0);
    lv_obj_add_event_cb(cancel_btn, cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *c_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(c_lbl, locale_get("print.cancel"));
    lv_obj_set_style_text_font(c_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(c_lbl);

    /* Status message */
    status_msg = lv_label_create(print_screen);
    lv_label_set_text(status_msg, "");
    lv_obj_set_style_text_color(status_msg, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_msg, &lv_font_montserrat_12, 0);
    lv_obj_align(status_msg, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Initial scan */
    refresh_btn_cb(NULL);

    return print_screen;
}

static void print_destroy(void)
{
    print_screen = NULL;
    file_list = NULL;
    status_msg = NULL;
}

const screen_ops_t screen_print = {
    .name = "Print from USB",
    .create = print_create,
    .destroy = print_destroy,
    .show_back = true,
};
