/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Stock material selection/import entry points.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include "material_catalog.h"
#include "print_profile.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DENEB_MATERIAL_IMPORT_ROOT "/mnt/sda1"

typedef struct {
    const char *label;
    const char *guid;
} material_choice_t;

static const material_choice_t materials[] = {
    {"Generic PLA", "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"},
    {"Generic Tough PLA", "9d5d2d7c-4e77-441c-85a0-e9eefd4aa68c"},
    {"Generic PETG", "1cbfaeb3-1906-4b26-b2e7-6f777a8c197a"},
    {"Generic ABS", "60636bb4-518f-42e7-8237-fe77b194ebe0"},
    {"Generic CPE", "12f41353-1a33-415e-8b4f-a775a6c70cc6"},
    {"Generic CPE+", "e2409626-b5a0-4025-b73e-b58070219259"},
    {"Generic Nylon", "28fb4162-db74-49e1-9008-d05f1e8bef5c"},
    {"Generic PC", "98c05714-bf4e-4455-ba27-57d74fe331e4"},
    {"Generic PP", "aa22e9c7-421f-4745-afc2-81851694394a"},
    {"Generic TPU 95A", "1d52b2be-a3a2-41de-a8b1-3bcdb5618695"},
};

static lv_obj_t *screen = NULL;
static lv_obj_t *status_label = NULL;

static const char *find_material_label(const char *guid)
{
    if (!guid || !*guid)
        return NULL;

    for (int i = 0; i < (int)(sizeof(materials) / sizeof(materials[0])); i++) {
        if (strcmp(materials[i].guid, guid) == 0)
            return materials[i].label;
    }

    return NULL;
}

static int has_material_extension(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (!dot)
        return 0;
    return strcmp(dot, ".xml") == 0 ||
           strcmp(dot, ".fdm_material") == 0 ||
           strcmp(dot, ".material") == 0;
}

static int import_material_tree(const char *root, int depth, int *imported)
{
    DIR *dir;
    struct dirent *ent;

    if (depth > 4)
        return 0;

    dir = opendir(root);
    if (!dir)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        char path[512];
        struct stat st;

        if (ent->d_name[0] == '.')
            continue;

        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        if (stat(path, &st) < 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            import_material_tree(path, depth + 1, imported);
        } else if (S_ISREG(st.st_mode) && has_material_extension(ent->d_name)) {
            char guid[64];
            int version = 0;

            if (deneb_material_catalog_store_file(path,
                                                  DENEB_MATERIAL_CATALOG_DIR,
                                                  guid, sizeof(guid),
                                                  &version) == 0) {
                (*imported)++;
            }
        }
    }

    closedir(dir);
    return 0;
}

static int import_usb_material_profiles(void)
{
    int imported = 0;

    if (import_material_tree(DENEB_MATERIAL_IMPORT_ROOT, 0, &imported) < 0)
        return -1;
    return imported;
}

static void update_current_material_status(void)
{
    char guid[64];
    const char *label = NULL;

    deneb_print_profile_read_loaded_material_guid(guid, sizeof(guid));
    label = find_material_label(guid);

    lv_label_set_text_fmt(status_label, locale_get("material.current_fmt"),
                          label ? label
                                : locale_get("material.current_unknown"));
}

static void set_material_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)(sizeof(materials) / sizeof(materials[0])))
        return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "uci -q set ultimaker.option.material_guid='%s'; "
             "uci -q commit ultimaker",
             materials[idx].guid);

    if (system(cmd) == 0)
        lv_label_set_text_fmt(status_label, locale_get("material.set_fmt"),
                              materials[idx].label);
    else
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
}

static void import_material_cb(lv_event_t *e)
{
    int imported;

    (void)e;
    lv_label_set_text(status_label, locale_get("material.importing"));
    imported = import_usb_material_profiles();
    if (imported >= 0) {
        lv_label_set_text_fmt(status_label, "Imported %d material profile%s",
                              imported, imported == 1 ? "" : "s");
    } else {
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
    }
}

static lv_obj_t *create_btn(lv_obj_t *parent, const char *label,
                            lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 292, 32);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, 270);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    return btn;
}

static lv_obj_t *set_material_create(void)
{
    screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(screen, 320, 208);
    lv_obj_align(screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 10, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(screen, 5, 0);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, locale_get("material.set"));
    lv_obj_set_style_text_color(title, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(title, &deneb_font_14, 0);

    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "");
    lv_obj_set_width(status_label, 292);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);
    update_current_material_status();

    for (int i = 0; i < (int)(sizeof(materials) / sizeof(materials[0])); i++)
        create_btn(screen, materials[i].label, set_material_cb,
                   (void *)(intptr_t)i);

    create_btn(screen, locale_get("material.import"), import_material_cb, NULL);

    return screen;
}

static void set_material_destroy(void)
{
    screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_set_material = {
    .name = "material.set",
    .create = set_material_create,
    .destroy = set_material_destroy,
    .show_back = true,
};
