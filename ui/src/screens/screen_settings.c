/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Settings screen. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *settings_screen = NULL;

extern const screen_ops_t screen_settings;
extern const screen_ops_t screen_about;

static int valid_lang_code(const char *lang)
{
    static const char *const supported[] = {
        "en", "nl", "de", "fr", "en-pirate", "en-1337", NULL
    };

    if (!lang)
        return 0;

    for (int i = 0; supported[i]; i++) {
        if (strcmp(lang, supported[i]) == 0)
            return 1;
    }

    return 0;
}

static void persist_lang_code(const char *lang)
{
    char cmd[128];

    if (!valid_lang_code(lang))
        return;

    snprintf(cmd, sizeof(cmd),
             "uci -q set deneb.system=system; "
             "uci -q set deneb.system.language='%s'; "
             "uci -q commit deneb",
             lang);
    system(cmd);
}

static void lang_btn_cb(lv_event_t *e)
{
    const char *lang = (const char *)lv_event_get_user_data(e);

    if (!valid_lang_code(lang))
        return;

    persist_lang_code(lang);
    locale_set(lang);
    screen_mgr_replace(&screen_settings);
}

static void about_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_push(&screen_about);
}

static lv_obj_t *settings_create(void)
{
    settings_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(settings_screen, 320, 208);
    lv_obj_align(settings_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(settings_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(settings_screen, 0, 0);
    lv_obj_set_style_border_width(settings_screen, 0, 0);
    lv_obj_set_style_pad_all(settings_screen, 10, 0);
    lv_obj_set_flex_flow(settings_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(settings_screen, 6, 0);

    lv_obj_t *lang_title = lv_label_create(settings_screen);
    lv_label_set_text(lang_title, locale_get("settings.language"));
    lv_obj_set_style_text_color(lang_title, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(lang_title, &lv_font_montserrat_14, 0);

    lv_obj_t *lang_row = lv_obj_create(settings_screen);
    lv_obj_set_size(lang_row, 300, 32);
    lv_obj_set_style_bg_opa(lang_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lang_row, 0, 0);
    lv_obj_set_style_pad_all(lang_row, 0, 0);
    lv_obj_set_flex_flow(lang_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(lang_row, 4, 0);
    lv_obj_remove_flag(lang_row, LV_OBJ_FLAG_SCROLLABLE);

    static const struct { const char *code; const char *label; } langs[] = {
        {"en", "EN"}, {"nl", "NL"}, {"de", "DE"}, {"fr", "FR"},
        {"en-pirate", "PR"}, {"en-1337", "L33T"},
    };

    for (int i = 0; i < (int)(sizeof(langs) / sizeof(langs[0])); i++) {
        lv_obj_t *btn = lv_button_create(lang_row);
        lv_obj_set_size(btn, 46, 28);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, lang_btn_cb, LV_EVENT_CLICKED,
                            (void *)langs[i].code);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, langs[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }

    lv_obj_t *sep = lv_obj_create(settings_screen);
    lv_obj_set_size(sep, 300, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    lv_obj_t *about_btn = lv_button_create(settings_screen);
    lv_obj_set_size(about_btn, 300, 36);
    lv_obj_set_style_bg_color(about_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(about_btn, 4, 0);
    lv_obj_add_event_cb(about_btn, about_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *about_lbl = lv_label_create(about_btn);
    lv_label_set_text_fmt(about_lbl, "%s %s", LV_SYMBOL_LIST,
                          locale_get("settings.about"));
    lv_obj_align(about_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    return settings_screen;
}

static void settings_destroy(void) { settings_screen = NULL; }

const screen_ops_t screen_settings = {
    .name = "Settings",
    .create = settings_create,
    .destroy = settings_destroy,
    .show_back = true,
};
