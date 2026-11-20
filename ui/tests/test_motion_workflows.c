/* SPDX-License-Identifier: MPL-2.0 */
#include "../src/screens/screen_level.c"
#define set_btn_enabled material_set_btn_enabled
#include "../src/screens/screen_material.c"
#undef set_btn_enabled
#include "../src/screens/screen_jog.c"
#include <assert.h>
#include <string.h>

static printer_state_t observed;
static uint64_t admitted_operation;
static int macro_rc;
static int abort_rc;
static lv_timer_cb_t active_timer;
static int timer_handle;
static const char *shown_status;

const printer_state_t *backend_get_state(void) { return &observed; }
int backend_is_ready(void) { return observed.connected && !observed.has_error; }
int backend_manual_action_allowed(void)
{
    return backend_is_ready() && observed.manual_state != DENEB_MANUAL_RUNNING;
}
int backend_send_gcodes(const char *const *lines, size_t count)
{
    assert(lines && count == 2);
    return 0;
}
int backend_send_macro(const char *macro) { assert(macro && *macro); return macro_rc; }
uint64_t backend_get_last_operation(void) { return admitted_operation; }
int backend_abort_print(void) { return abort_rc; }
const char *locale_get(const char *key) { return key; }
uint32_t lv_tick_get(void) { return 100; }
void lv_obj_add_state(lv_obj_t *obj, lv_state_t state) { (void)obj; (void)state; }
void lv_obj_remove_state(lv_obj_t *obj, lv_state_t state) { (void)obj; (void)state; }
void lv_label_set_text(lv_obj_t *obj, const char *text)
{
    (void)obj;
    shown_status = text;
}
void lv_label_set_text_fmt(lv_obj_t *obj, const char *fmt, ...)
{
    (void)obj;
    (void)fmt;
}
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *data)
{
    (void)period;
    (void)data;
    active_timer = cb;
    return (lv_timer_t *)&timer_handle;
}
void lv_timer_delete(lv_timer_t *timer)
{
    assert(timer == (lv_timer_t *)&timer_handle);
    active_timer = NULL;
}

static void test_level_failed_abort(bool leaving_screen)
{
    deneb_buildplate_level_workflow_init(&level_workflow);
    admitted_operation = 7;
    observed.manual_id = admitted_operation;
    observed.manual_state = DENEB_MANUAL_NONE;
    level_next_cb(NULL);
    assert(level_workflow.moving && active_timer);

    observed.manual_state = DENEB_MANUAL_RUNNING;
    abort_rc = -1;
    if (leaving_screen)
        level_destroy();
    else
        level_cancel_cb(NULL);
    assert(level_workflow.state == DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED);
    assert(!level_workflow.moving && !active_timer);
    observed.manual_state = DENEB_MANUAL_COMPLETE;
    assert(strcmp(level_status_text(), "level.failed") == 0);
    assert(level_workflow.step_count == 0);
}

static void test_material_ack_requires_token(void)
{
    lv_event_cb_t callbacks[] = {workflow_load_cb, workflow_unload_cb};
    workflow_status_label = (lv_obj_t *)&timer_handle;
    observed.nozzle_temp_cur = 190;
    for (size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); ++i) {
        deneb_material_workflow_init(&workflow_state);
        assert(deneb_material_workflow_prepare(
            &workflow_state, DENEB_MATERIAL_WORKFLOW_OP_CHANGE) == 0);
        deneb_material_workflow_set_target(&workflow_state, 190, 1);
        admitted_operation = 0;
        callbacks[i](NULL);
        assert(!workflow_state.moving);
        assert(strcmp(shown_status, "material.move_unconfirmed") == 0);
        admitted_operation = 8 + i;
        observed.manual_state = DENEB_MANUAL_COMPLETE;
        callbacks[i](NULL);
        /* The ACK arrives ahead of the publication for the new operation. */
        observed.manual_id = admitted_operation;
        observed.manual_state = DENEB_MANUAL_RUNNING;
        workflow_update();
        assert(workflow_state.moving && workflow_operation_id == admitted_operation);
        observed.manual_state = DENEB_MANUAL_COMPLETE;
        workflow_update();
        assert(!workflow_state.moving);
    }
}

static void test_jog_rejection_feedback(void)
{
    jog_status_label = (lv_obj_t *)&timer_handle;
    observed.manual_state = DENEB_MANUAL_RUNNING;
    home_xy_btn_cb(NULL);
    assert(strcmp(shown_status, "jog.busy") == 0);

    observed.manual_state = DENEB_MANUAL_COMPLETE;
    macro_rc = -1;
    home_xy_btn_cb(NULL);
    assert(strcmp(shown_status, "jog.unconfirmed") == 0);
}

int main(void)
{
    observed.connected = true;
    test_level_failed_abort(false);
    test_level_failed_abort(true);
    test_material_ack_requires_token();
    test_jog_rejection_feedback();
    return 0;
}
