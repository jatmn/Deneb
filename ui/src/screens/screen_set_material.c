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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *screen = NULL;
static lv_obj_t *status_label = NULL;

static int import_usb_material_profiles(void)
{
    int imported = 0;

    if (deneb_material_catalog_import_tree(DENEB_MATERIAL_IMPORT_USB_ROOT,
                                           DENEB_MATERIAL_CATALOG_DIR,
                                           DENEB_MATERIAL_IMPORT_MAX_DEPTH,
                                           &imported) < 0)
        return -1;
    return imported;
}

static void update_current_material_status(void)
{
    char guid[64];
    const char *label = NULL;

    deneb_print_profile_read_loaded_material_guid(guid, sizeof(guid));
    label = deneb_print_profile_material_label_from_guid(guid);

    lv_label_set_text_fmt(status_label, locale_get("material.current_fmt"),
                          label ? label
                                : locale_get("material.current_unknown"));
}

static void set_material_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const deneb_print_profile_material_choice_t *choice;
    char cmd[256];

    if (idx < 0)
        return;
    choice = deneb_print_profile_material_choice((size_t)idx);
    if (!choice)
        return;

    if (deneb_print_profile_format_set_material_command(choice->guid, cmd,
                                                        sizeof(cmd)) < 0)
        return;

    if (system(cmd) == 0)
        lv_label_set_text_fmt(status_label, locale_get("material.set_fmt"),
                              choice->label);
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

    for (size_t i = 0; i < deneb_print_profile_material_choice_count(); i++) {
        const deneb_print_profile_material_choice_t *choice =
            deneb_print_profile_material_choice(i);
        if (choice)
            create_btn(screen, choice->label, set_material_cb,
                       (void *)(intptr_t)i);
    }

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
