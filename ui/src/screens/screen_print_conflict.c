/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Network print configuration conflict prompt.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include "backend_comm.h"
#include "command_format.h"
#include "pending_job_file.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *conflict_screen = NULL;
static lv_obj_t *message_label = NULL;
static lv_obj_t *detail_label = NULL;
static lv_obj_t *status_label = NULL;
static int current_pending = 0;

int print_conflict_has_pending(void)
{
    return deneb_pending_job_file_has_conflict_default();
}

static void finish_prompt(const char *status_key)
{
    if (status_label)
        lv_label_set_text(status_label, locale_get(status_key));
    screen_mgr_pop();
}

static void continue_btn_cb(lv_event_t *e)
{
    (void)e;
    if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.continuing"));
    if (backend_send_pending_instruction(DENEB_PRINT_REQ_PREPARE) == 0) {
        finish_prompt("print_conflict.continuing");
    } else if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.action_failed"));
}

static void cancel_btn_cb(lv_event_t *e)
{
    (void)e;
    if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.cancelled"));
    if (backend_send_pending_instruction(DENEB_COMMAND_VERB_ABORT) == 0)
        finish_prompt("print_conflict.cancelled");
    else if (status_label)
        lv_label_set_text(status_label, locale_get("print_conflict.action_failed"));
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             lv_event_cb_t cb, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 138, 34);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_center(lbl);
    return btn;
}

static void load_prompt_text(void)
{
    deneb_pending_job_conflict_prompt_t prompt;
    char msg[256];
    char detail[160];

    current_pending = 0;
    deneb_pending_job_file_conflict_prompt_default(&prompt);
    current_pending = prompt.is_pending;

    locale_format_ss(msg, sizeof(msg), "print_conflict.message_fmt",
                     prompt.target_name, prompt.loaded_name);
    locale_format_s(detail, sizeof(detail), "print_conflict.job_fmt",
                    prompt.job_name);

    lv_label_set_text(message_label, msg);
    lv_label_set_text(detail_label, detail);
    lv_label_set_text(status_label, current_pending ? "" :
                      locale_get("print_conflict.action_failed"));
}

static lv_obj_t *conflict_create(void)
{
    conflict_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(conflict_screen, 320, 208);
    lv_obj_align(conflict_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(conflict_screen, lv_color_hex(0x151515), 0);
    lv_obj_set_style_radius(conflict_screen, 0, 0);
    lv_obj_set_style_border_width(conflict_screen, 0, 0);
    lv_obj_set_style_pad_all(conflict_screen, 10, 0);
    lv_obj_set_flex_flow(conflict_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(conflict_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(conflict_screen, 7, 0);

    lv_obj_t *icon = lv_label_create(conflict_screen);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xffc924), 0);
    lv_obj_set_style_text_font(icon, &deneb_font_16, 0);

    message_label = lv_label_create(conflict_screen);
    lv_obj_set_width(message_label, 292);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(0xf2f2f2), 0);
    lv_obj_set_style_text_font(message_label, &deneb_font_12, 0);

    detail_label = lv_label_create(conflict_screen);
    lv_obj_set_width(detail_label, 292);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(0xa8b0bd), 0);
    lv_obj_set_style_text_font(detail_label, &deneb_font_12, 0);

    lv_obj_t *actions = lv_obj_create(conflict_screen);
    lv_obj_set_size(actions, 300, 40);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    make_button(actions, locale_get("print_conflict.cancel"), cancel_btn_cb,
                lv_color_hex(0x4a4f59));
    make_button(actions, locale_get("print_conflict.continue"), continue_btn_cb,
                lv_color_hex(0x1d5fd3));

    status_label = lv_label_create(conflict_screen);
    lv_obj_set_width(status_label, 292);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa8b0bd), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    load_prompt_text();
    return conflict_screen;
}

static void conflict_destroy(void)
{
    conflict_screen = NULL;
    message_label = NULL;
    detail_label = NULL;
    status_label = NULL;
    current_pending = 0;
}

const screen_ops_t screen_print_conflict = {
    .name = "print_conflict.title",
    .create = conflict_create,
    .destroy = conflict_destroy,
    .show_back = false,
};
