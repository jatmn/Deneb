/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Language selection screen.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *language_screen = NULL;
static lv_obj_t *status_label = NULL;
static int show_saved_message = 0;

extern const screen_ops_t screen_language;

typedef struct {
    const char *code;
    const char *label_key;
} language_choice_t;

static const language_choice_t languages[] = {
    {"en", "language.en"},
    {"nl", "language.nl"},
    {"de", "language.de"},
    {"fr", "language.fr"},
    {"zh-Hans", "language.zh_Hans"},
    {"en-pirate", "language.en_pirate"},
    {"en-1337", "language.en_1337"},
};

static int valid_lang_code(const char *lang)
{
    if (!lang)
        return 0;

    for (int i = 0; i < (int)(sizeof(languages) / sizeof(languages[0])); i++) {
        if (strcmp(lang, languages[i].code) == 0)
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
    show_saved_message = 1;
    screen_mgr_rebuild_stack();
}

static lv_obj_t *language_create(void)
{
    language_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(language_screen, 320, 208);
    lv_obj_align(language_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(language_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(language_screen, 0, 0);
    lv_obj_set_style_border_width(language_screen, 0, 0);
    lv_obj_set_style_pad_all(language_screen, 10, 0);
    lv_obj_set_flex_flow(language_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(language_screen, 6, 0);
    lv_obj_set_scroll_dir(language_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(language_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(language_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(language_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    for (int i = 0; i < (int)(sizeof(languages) / sizeof(languages[0])); i++) {
        lv_obj_t *btn = lv_button_create(language_screen);
        lv_obj_set_size(btn, 300, 34);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_add_event_cb(btn, lang_btn_cb, LV_EVENT_CLICKED,
                            (void *)languages[i].code);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, locale_get(languages[i].label_key));
        lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    }

    status_label = lv_label_create(language_screen);
    lv_label_set_text(status_label,
                      show_saved_message ? locale_get("language.saved") : "");
    show_saved_message = 0;
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    return language_screen;
}

static void language_destroy(void)
{
    language_screen = NULL;
    status_label = NULL;
}

const screen_ops_t screen_language = {
    .name = "settings.language",
    .create = language_create,
    .destroy = language_destroy,
    .show_back = true,
};
