/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Screen manager. LVGL v9 API.
 */

#include "screen_mgr.h"
#include "locale.h"

#include <stdio.h>
#include <string.h>

static const screen_ops_t *stack[SCREEN_STACK_MAX];
static lv_obj_t *screen_objs[SCREEN_STACK_MAX];
static int stack_top = -1;
static lv_obj_t *header_bar = NULL;
static lv_obj_t *header_title = NULL;
static lv_obj_t *back_btn = NULL;

static void update_header(void);
static void invalidate_screen(void);
static void back_btn_click_cb(lv_event_t *e);

void screen_mgr_init(void)
{
    stack_top = -1;
    memset(stack, 0, sizeof(stack));
    memset(screen_objs, 0, sizeof(screen_objs));

    header_bar = lv_obj_create(lv_screen_active());
    lv_obj_set_size(header_bar, 320, 32);
    lv_obj_align(header_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(header_bar, 0, 0);
    lv_obj_set_style_border_width(header_bar, 0, 0);
    lv_obj_set_style_pad_all(header_bar, 4, 0);
    lv_obj_set_style_text_font(header_bar, &deneb_font_14, 0);
    lv_obj_remove_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    back_btn = lv_button_create(header_bar);
    lv_obj_set_size(back_btn, 48, 28);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(back_btn, 4, 0);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_btn_click_cb, LV_EVENT_CLICKED, NULL);

    header_title = lv_label_create(header_bar);
    lv_obj_set_style_text_color(header_title, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(header_title, &deneb_font_14, 0);
    lv_obj_align(header_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(header_bar, LV_OBJ_FLAG_HIDDEN);
}

void screen_mgr_push(const screen_ops_t *ops)
{
    if (stack_top >= SCREEN_STACK_MAX - 1) {
        fprintf(stderr, "screen_mgr: stack full, cannot push %s\n", ops->name);
        return;
    }

    if (stack_top >= 0 && screen_objs[stack_top]) {
        lv_obj_add_flag(screen_objs[stack_top], LV_OBJ_FLAG_HIDDEN);
    }

    stack_top++;
    stack[stack_top] = ops;
    screen_objs[stack_top] = ops->create();
    lv_obj_set_style_text_font(screen_objs[stack_top], &deneb_font_14, 0);

    update_header();
    invalidate_screen();
}

void screen_mgr_pop(void)
{
    if (stack_top <= 0) {
        fprintf(stderr, "screen_mgr: cannot pop root screen\n");
        return;
    }

    if (stack[stack_top]->destroy)
        stack[stack_top]->destroy();
    if (screen_objs[stack_top]) {
        lv_obj_delete(screen_objs[stack_top]);
        screen_objs[stack_top] = NULL;
    }
    stack[stack_top] = NULL;
    stack_top--;

    if (screen_objs[stack_top])
        lv_obj_remove_flag(screen_objs[stack_top], LV_OBJ_FLAG_HIDDEN);

    update_header();
    invalidate_screen();
}

void screen_mgr_replace(const screen_ops_t *ops)
{
    if (stack_top < 0) {
        screen_mgr_push(ops);
        return;
    }

    if (stack[stack_top]->destroy)
        stack[stack_top]->destroy();
    if (screen_objs[stack_top]) {
        lv_obj_delete(screen_objs[stack_top]);
        screen_objs[stack_top] = NULL;
    }

    stack[stack_top] = ops;
    screen_objs[stack_top] = ops->create();
    lv_obj_set_style_text_font(screen_objs[stack_top], &deneb_font_14, 0);

    update_header();
    invalidate_screen();
}

void screen_mgr_rebuild_stack(void)
{
    if (stack_top < 0)
        return;

    for (int i = 0; i <= stack_top; i++) {
        if (stack[i] && stack[i]->destroy)
            stack[i]->destroy();
        if (screen_objs[i]) {
            lv_obj_delete(screen_objs[i]);
            screen_objs[i] = NULL;
        }
    }

    for (int i = 0; i <= stack_top; i++) {
        if (!stack[i] || !stack[i]->create)
            continue;

        screen_objs[i] = stack[i]->create();
        lv_obj_set_style_text_font(screen_objs[i], &deneb_font_14, 0);

        if (i < stack_top)
            lv_obj_add_flag(screen_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    update_header();
    invalidate_screen();
}

const char *screen_mgr_current_name(void)
{
    if (stack_top >= 0 && stack[stack_top])
        return stack[stack_top]->name;
    return "(none)";
}

int screen_mgr_depth(void)
{
    return stack_top + 1;
}

static void update_header(void)
{
    if (stack_top < 0) {
        lv_obj_add_flag(header_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(header_bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(header_title, locale_get(stack[stack_top]->name));

    if (stack_top > 0 && stack[stack_top]->show_back) {
        lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(header_title, LV_ALIGN_CENTER, 16, 0);
    } else {
        lv_obj_add_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(header_title, LV_ALIGN_CENTER, 0, 0);
    }
}

static void invalidate_screen(void)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

static void back_btn_click_cb(lv_event_t *e)
{
    (void)e;
    screen_mgr_pop();
}
