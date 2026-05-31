/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Error / ER code display screen.
 * Shows error code, description, and recommended action.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"

static lv_obj_t *error_screen = NULL;
static lv_obj_t *er_code_label = NULL;
static lv_obj_t *er_desc_label = NULL;
static lv_obj_t *er_action_label = NULL;

static void dismiss_btn_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_pop();
}

/**
 * Show an error screen with the given ER code and description.
 * Called from the main status polling loop when an error is detected.
 */
void error_screen_show(const char *er_code, const char *description,
                       const char *action)
{
    if (!error_screen) return;

    if (er_code_label)
        lv_label_set_text(er_code_label, er_code ? er_code : "???");
    if (er_desc_label)
        lv_label_set_text(er_desc_label, description ? description : "");
    if (er_action_label)
        lv_label_set_text(er_action_label, action ? action : "");
}

static lv_obj_t *error_create(void)
{
    error_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(error_screen, 320, 208);
    lv_obj_align(error_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(error_screen, lv_color_hex(0x1a0a0a), 0);
    lv_obj_set_style_radius(error_screen, 0, 0);
    lv_obj_set_style_border_width(error_screen, 0, 0);
    lv_obj_set_style_pad_all(error_screen, 12, 0);
    lv_obj_set_flex_flow(error_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(error_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(error_screen, 8, 0);

    /* Error icon */
    lv_obj_t *icon = lv_label_create(error_screen);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);

    /* ER code */
    er_code_label = lv_label_create(error_screen);
    lv_label_set_text(er_code_label, "ER???");
    lv_obj_set_style_text_color(er_code_label, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(er_code_label, &lv_font_montserrat_16, 0);

    /* Description */
    er_desc_label = lv_label_create(error_screen);
    lv_label_set_text(er_desc_label, "");
    lv_obj_set_style_text_color(er_desc_label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(er_desc_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(er_desc_label, 280);
    lv_label_set_long_mode(er_desc_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(er_desc_label, LV_TEXT_ALIGN_CENTER, 0);

    /* Recommended action */
    er_action_label = lv_label_create(error_screen);
    lv_label_set_text(er_action_label, "");
    lv_obj_set_style_text_color(er_action_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(er_action_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(er_action_label, 280);
    lv_label_set_long_mode(er_action_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(er_action_label, LV_TEXT_ALIGN_CENTER, 0);

    /* Dismiss button */
    lv_obj_t *ok_btn = lv_button_create(error_screen);
    lv_obj_set_size(ok_btn, 120, 32);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(ok_btn, 6, 0);
    lv_obj_add_event_cb(ok_btn, dismiss_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, locale_get("error.ok"));
    lv_obj_center(ok_lbl);

    return error_screen;
}

static void error_destroy(void)
{
    error_screen = NULL;
    er_code_label = NULL;
    er_desc_label = NULL;
    er_action_label = NULL;
}

const screen_ops_t screen_error = {
    .name = "Error",
    .create = error_create,
    .destroy = error_destroy,
    .show_back = false, /* Must dismiss via OK, not back */
};
