/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb package update screen.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UPDATES 16
#define MAX_PATH    160
#define MAX_NAME    80

static lv_obj_t *update_screen = NULL;
static lv_obj_t *list = NULL;
static lv_obj_t *status_label = NULL;
static char update_paths[MAX_UPDATES][MAX_PATH];
static char update_names[MAX_UPDATES][MAX_NAME];
static int update_count = 0;
static int confirm_idx = -1;

static void check_releases_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text(status_label, locale_get("update.check_pending"));
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    return name_len > suffix_len &&
           strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void shell_quote(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    if (dst_size == 0)
        return;
    dst[out++] = '\'';
    while (*src && out + 5 < dst_size) {
        if (*src == '\'') {
            memcpy(dst + out, "'\\''", 4);
            out += 4;
        } else {
            dst[out++] = *src;
        }
        src++;
    }
    if (out + 1 < dst_size)
        dst[out++] = '\'';
    dst[out] = '\0';
}

static void scan_dir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && update_count < MAX_UPDATES) {
        if (!has_suffix(ent->d_name, ".deneb"))
            continue;

        snprintf(update_paths[update_count], MAX_PATH, "%s/%s",
                 path, ent->d_name);
        strncpy(update_names[update_count], ent->d_name, MAX_NAME - 1);
        update_names[update_count][MAX_NAME - 1] = '\0';
        update_count++;
    }

    closedir(dir);
}

static void scan_updates(void)
{
    update_count = 0;
    confirm_idx = -1;
    memset(update_paths, 0, sizeof(update_paths));
    memset(update_names, 0, sizeof(update_names));
    scan_dir("/mnt/sda1");
    scan_dir("/mnt/usb");
    scan_dir("/media/usb");
}

static void update_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= update_count)
        return;

    if (confirm_idx != idx) {
        confirm_idx = idx;
        lv_label_set_text_fmt(status_label, locale_get("update.tap_again_fmt"),
                              update_names[idx]);
        return;
    }

    char quoted[MAX_PATH * 5];
    char cmd[768];
    shell_quote(quoted, sizeof(quoted), update_paths[idx]);
    snprintf(cmd, sizeof(cmd),
             "(rm -rf /tmp/update && mkdir -p /tmp/update && "
             "tar xf %s -C /tmp/update && "
             "chmod +x /tmp/update/update.sh && "
             "/tmp/update/update.sh) >/tmp/deneb-update.log 2>&1 &",
             quoted);

    lv_label_set_text(status_label, locale_get("update.started"));
    confirm_idx = -1;
    system(cmd);
}

static void refresh_list(void)
{
    if (list) {
        lv_obj_delete(list);
        list = NULL;
    }

    scan_updates();

    list = lv_obj_create(update_screen);
    lv_obj_set_size(list, 300, 140);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x0f0f23), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    if (update_count == 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, locale_get("update.no_files"));
        lv_obj_set_style_text_color(empty, lv_color_hex(0xa0a0a0), 0);
        return;
    }

    for (int i = 0; i < update_count; i++) {
        lv_obj_t *btn = lv_button_create(list);
        lv_obj_set_size(btn, 288, 32);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, update_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, update_names[i]);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(lbl, 268);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
    }
}

static lv_obj_t *update_create(void)
{
    update_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(update_screen, 320, 208);
    lv_obj_align(update_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(update_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(update_screen, 0, 0);
    lv_obj_set_style_border_width(update_screen, 0, 0);
    lv_obj_set_style_pad_all(update_screen, 10, 0);
    lv_obj_set_flex_flow(update_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(update_screen, 8, 0);

    status_label = lv_label_create(update_screen);
    lv_label_set_text(status_label, locale_get("update.select"));
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    lv_obj_t *check_btn = lv_button_create(update_screen);
    lv_obj_set_size(check_btn, 300, 30);
    lv_obj_set_style_bg_color(check_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(check_btn, 4, 0);
    lv_obj_add_event_cb(check_btn, check_releases_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *check_lbl = lv_label_create(check_btn);
    lv_label_set_text(check_lbl, locale_get("update.check_releases"));
    lv_obj_set_style_text_font(check_lbl, &deneb_font_12, 0);
    lv_obj_center(check_lbl);

    refresh_list();
    return update_screen;
}

static void update_destroy(void)
{
    update_screen = NULL;
    list = NULL;
    status_label = NULL;
    confirm_idx = -1;
}

const screen_ops_t screen_update = {
    .name = "maintenance.update_firmware",
    .create = update_create,
    .destroy = update_destroy,
    .show_back = true,
};
