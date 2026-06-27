/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Build plate leveling guided workflow using Deneb-owned native macro files.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "backend_comm.h"
#include "buildplate_level.h"
#include "lvgl.h"
#include <stdint.h>
#include <pthread.h>

static lv_obj_t *level_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *next_btn = NULL;
static lv_obj_t *next_btn_label = NULL;
static lv_obj_t *cancel_btn = NULL;
static lv_timer_t *level_macro_timer = NULL;
static deneb_buildplate_level_workflow_t level_workflow;
static volatile int level_macro_running = 0;
static volatile int level_macro_done = 0;
static volatile int level_macro_rc = 0;
static volatile int level_macro_cancelled = 0;
static const char *level_macro_name = NULL;

static int level_backend_ready(void)
{
    return backend_is_ready() && backend_manual_action_allowed();
}

static const char *level_step_locale_key(deneb_buildplate_level_step_t step)
{
    switch (step) {
        case DENEB_BUILDPLATE_LEVEL_STEP_1:
            return "level.step1";
        case DENEB_BUILDPLATE_LEVEL_STEP_2:
            return "level.step2";
        case DENEB_BUILDPLATE_LEVEL_STEP_3:
            return "level.step3";
        case DENEB_BUILDPLATE_LEVEL_STEP_4:
            return "level.step4";
        case DENEB_BUILDPLATE_LEVEL_STEP_FINISH:
        default:
            return "level.finish";
    }
}

static const char *level_status_text(void)
{
    if (!level_backend_ready())
        return locale_get("level.busy");
    switch (level_workflow.state) {
        case DENEB_BUILDPLATE_LEVEL_STATE_MOVING:
            if (level_macro_cancelled)
                return locale_get("level.cancelling");
            return locale_get("level.moving");
        case DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET:
            return locale_get("level.ready_next");
        case DENEB_BUILDPLATE_LEVEL_STATE_FINAL:
            return locale_get("level.done");
        case DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED:
            if (level_macro_rc != 0)
                return locale_get("level.failed");
            return locale_get("level.cancelled");
        case DENEB_BUILDPLATE_LEVEL_STATE_PREPARED:
        case DENEB_BUILDPLATE_LEVEL_STATE_NONE:
        default:
            return locale_get("level.message");
    }
}

static void set_btn_enabled(lv_obj_t *btn, int enabled)
{
    if (!btn)
        return;
    if (enabled)
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
    else
        lv_obj_add_state(btn, LV_STATE_DISABLED);
}

static void level_update(void)
{
    deneb_buildplate_level_step_t next;
    int can_run_next;
    can_run_next = level_backend_ready() && !level_workflow.moving &&
        !level_macro_running;
    if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_PREPARED ||
        level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET) {
        can_run_next = can_run_next &&
            deneb_buildplate_level_workflow_next_step(&level_workflow,
                                                     &next) == 0;
        if (next_btn_label && can_run_next)
            lv_label_set_text(next_btn_label,
                              locale_get(level_step_locale_key(next)));
    } else if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_NONE ||
               level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_FINAL ||
               level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED) {
        if (next_btn_label)
            lv_label_set_text(next_btn_label, locale_get("level.step1"));
    } else {
        can_run_next = 0;
    }

    if (status_label)
        lv_label_set_text(status_label, level_status_text());

    set_btn_enabled(next_btn, can_run_next);
    set_btn_enabled(cancel_btn,
                    (level_workflow.state ==
                         DENEB_BUILDPLATE_LEVEL_STATE_PREPARED ||
                     level_workflow.state ==
                         DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET ||
                     level_workflow.state ==
                         DENEB_BUILDPLATE_LEVEL_STATE_MOVING) &&
                    !level_macro_cancelled);
}


static int level_prepare_if_needed(void)
{
    if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_NONE ||
        level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_FINAL ||
        level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED)
        return deneb_buildplate_level_workflow_prepare(&level_workflow);
    return 0;
}

static void *level_macro_thread(void *arg)
{
    const char *macro = (const char *)arg;

    level_macro_rc = backend_send_macro(macro);
    level_macro_done = 1;
    level_macro_running = 0;
    return NULL;
}

static void level_macro_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!level_macro_done)
        return;

    level_macro_done = 0;
    if (level_macro_timer) {
        lv_timer_delete(level_macro_timer);
        level_macro_timer = NULL;
    }

    if (level_macro_cancelled) {
        level_macro_rc = 0;
        deneb_buildplate_level_workflow_cancel(&level_workflow);
    } else if (level_macro_rc == 0) {
        deneb_buildplate_level_workflow_complete_move(&level_workflow);
    } else if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_MOVING) {
        deneb_buildplate_level_workflow_cancel(&level_workflow);
    }
    level_macro_name = NULL;
    level_macro_cancelled = 0;
    level_update();
}

static void level_next_cb(lv_event_t *e)
{
    deneb_buildplate_level_step_t next;
    deneb_buildplate_level_plan_t plan;
    int transition_rc;
    pthread_t tid;

    (void)e;
    if (!level_backend_ready() || level_macro_running) {
        level_update();
        return;
    }
    if (level_prepare_if_needed() != 0 ||
        deneb_buildplate_level_workflow_next_step(&level_workflow,
                                                 &next) != 0 ||
        deneb_buildplate_level_plan_step(next, &plan) != 0) {
        level_update();
        return;
    }

    if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_PREPARED)
        transition_rc = deneb_buildplate_level_workflow_start(&level_workflow);
    else
        transition_rc = deneb_buildplate_level_workflow_advance(&level_workflow,
                                                               next);
    if (transition_rc != 0) {
        level_update();
        return;
    }

    level_macro_name = plan.macro;
    level_macro_rc = 0;
    level_macro_done = 0;
    level_macro_cancelled = 0;
    level_macro_running = 1;
    level_update();

    if (pthread_create(&tid, NULL, level_macro_thread,
                       (void *)level_macro_name) != 0) {
        level_macro_running = 0;
        level_macro_done = 0;
        level_macro_rc = -1;
        deneb_buildplate_level_workflow_cancel(&level_workflow);
        level_update();
        return;
    }
    pthread_detach(tid);
    if (!level_macro_timer)
        level_macro_timer = lv_timer_create(level_macro_timer_cb, 100, NULL);
}

static void level_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_MOVING) {
        level_macro_cancelled = 1;
        level_update();
        return;
    }
    deneb_buildplate_level_workflow_cancel(&level_workflow);
    level_update();
}

static lv_obj_t *create_level_btn(lv_obj_t *parent, const char *label,
                                  lv_event_cb_t cb, lv_color_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 142, 34);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x343448), LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &deneb_font_12, 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *level_create(void)
{
    if (level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_NONE) {
        deneb_buildplate_level_workflow_init(&level_workflow);
        deneb_buildplate_level_workflow_prepare(&level_workflow);
    }

    level_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(level_screen, 320, 208);
    lv_obj_align(level_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(level_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(level_screen, 0, 0);
    lv_obj_set_style_border_width(level_screen, 0, 0);
    lv_obj_set_style_pad_all(level_screen, 10, 0);
    lv_obj_set_flex_flow(level_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(level_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(level_screen, 8, 0);
    lv_obj_set_scroll_dir(level_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(level_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(level_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(level_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    status_label = lv_label_create(level_screen);
    lv_label_set_text(status_label, locale_get("level.message"));
    lv_obj_set_width(status_label, 300);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(status_label, &deneb_font_12, 0);

    lv_obj_t *actions = lv_obj_create(level_screen);
    lv_obj_set_size(actions, 300, 40);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    next_btn = lv_button_create(actions);
    lv_obj_set_size(next_btn, 142, 34);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x1f7a5a), 0);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x343448),
                              LV_STATE_DISABLED);
    lv_obj_set_style_radius(next_btn, 4, 0);
    lv_obj_add_event_cb(next_btn, level_next_cb, LV_EVENT_CLICKED, NULL);
    next_btn_label = lv_label_create(next_btn);
    lv_label_set_text(next_btn_label, locale_get("level.step1"));
    lv_obj_set_style_text_font(next_btn_label, &deneb_font_12, 0);
    lv_obj_center(next_btn_label);

    cancel_btn = create_level_btn(actions, locale_get("print.cancel"),
                                  level_cancel_cb, lv_color_hex(0xe94560));

    level_update();

    return level_screen;
}

static void level_destroy(void)
{
    if (level_macro_timer && !level_macro_running) {
        lv_timer_delete(level_macro_timer);
        level_macro_timer = NULL;
    }
    level_screen = NULL;
    status_label = NULL;
    next_btn = NULL;
    next_btn_label = NULL;
    cancel_btn = NULL;
}

const screen_ops_t screen_level = {
    .name = "maintenance.level_buildplate",
    .create = level_create,
    .destroy = level_destroy,
    .show_back = true,
};
