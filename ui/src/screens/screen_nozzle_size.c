/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Nozzle size setting. Matches stock UCI option: ultimaker.option.nozzle_size.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include "print_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *nozzle_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *size_buttons[4] = {NULL, NULL, NULL, NULL};
static const char *size_values[] = {"0.25", "0.4", "0.6", "0.8"};
static const char *size_labels[] = {"0.25 mm", "0.40 mm", "0.60 mm", "0.80 mm"};
static char selected_size[8] = "0.4";

static void load_selected_size(void)
{
    deneb_print_profile_read_loaded_nozzle_size(selected_size,
                                                sizeof(selected_size));
    deneb_print_profile_normalize_nozzle_size(selected_size, selected_size,
                                              sizeof(selected_size));
}

static const char *display_label_for_size(const char *size)
{
    for (size_t i = 0; i < sizeof(size_values) / sizeof(size_values[0]); i++) {
        if (strcmp(size_values[i], size) == 0)
            return size_labels[i];
    }
    return "0.40 mm";
}

static void style_size_button(lv_obj_t *btn, int selected)
{
    lv_obj_set_style_bg_color(btn,
                              lv_color_hex(selected ? 0x53a8b6 : 0x16213e),
                              0);
    lv_obj_set_style_border_width(btn, selected ? 2 : 0, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xe0e0e0), 0);
}

static void update_button_highlights(void)
{
    for (size_t i = 0; i < sizeof(size_values) / sizeof(size_values[0]); i++) {
        if (size_buttons[i])
            style_size_button(size_buttons[i],
                              strcmp(selected_size, size_values[i]) == 0);
    }
}

static void size_cb(lv_event_t *e)
{
    const char *size = (const char *)lv_event_get_user_data(e);
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "uci set ultimaker.option.nozzle_size=%s && uci commit ultimaker",
             size);
    if (system(cmd) == 0) {
        snprintf(selected_size, sizeof(selected_size), "%s", size);
        update_button_highlights();
        lv_label_set_text(status_label, display_label_for_size(size));
    } else {
        lv_label_set_text(status_label, locale_get("settings.save_failed"));
    }
}

static void make_btn(lv_obj_t *parent, size_t index)
{
    const char *size = size_values[index];
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 140, 36);
    size_buttons[index] = btn;
    style_size_button(btn, strcmp(selected_size, size) == 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, size_cb, LV_EVENT_CLICKED, (void *)size);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, size_labels[index]);
    lv_obj_center(lbl);
}

static lv_obj_t *nozzle_create(void)
{
    load_selected_size();

    nozzle_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(nozzle_screen, 320, 208);
    lv_obj_align(nozzle_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(nozzle_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(nozzle_screen, 0, 0);
    lv_obj_set_style_border_width(nozzle_screen, 0, 0);
    lv_obj_set_style_pad_all(nozzle_screen, 12, 0);
    lv_obj_set_flex_flow(nozzle_screen, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(nozzle_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(nozzle_screen, 8, 0);

    for (size_t i = 0; i < sizeof(size_values) / sizeof(size_values[0]); i++)
        make_btn(nozzle_screen, i);

    status_label = lv_label_create(nozzle_screen);
    lv_obj_set_width(status_label, 280);
    lv_label_set_text(status_label, display_label_for_size(selected_size));
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);

    return nozzle_screen;
}

static void nozzle_destroy(void)
{
    nozzle_screen = NULL;
    status_label = NULL;
    for (size_t i = 0; i < sizeof(size_buttons) / sizeof(size_buttons[0]); i++)
        size_buttons[i] = NULL;
}

const screen_ops_t screen_nozzle_size = {
    .name = "settings.nozzle_size",
    .create = nozzle_create,
    .destroy = nozzle_destroy,
    .show_back = true,
};
