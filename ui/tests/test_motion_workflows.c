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
static uint64_t cancel_requested;
static lv_timer_cb_t active_timer;
static int timer_handle;
static const char *shown_status;
static char sent_gcode[32];

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
int backend_send_gcode(const char *line)
{
    assert(line && strlen(line) < sizeof(sent_gcode));
    strcpy(sent_gcode, line);
    return 0;
}
int backend_send_macro(const char *macro) { assert(macro && *macro); return macro_rc; }
uint64_t backend_get_last_operation(void) { return admitted_operation; }
int backend_cancel_manual(uint64_t operation)
{
    cancel_requested = operation;
    return abort_rc;
}
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
void lv_slider_set_value(lv_obj_t *obj, int32_t value, lv_anim_enable_t anim)
{
    (void)obj;
    (void)value;
    (void)anim;
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
    assert(cancel_requested == admitted_operation);
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
        if (i == 1) {
            observed.manual_id++;
            workflow_stop_cb(NULL);
            assert(cancel_requested == admitted_operation);
            assert(strcmp(shown_status, "material.stop_unconfirmed") == 0);
            observed.manual_id = admitted_operation;
        }
        observed.manual_state = DENEB_MANUAL_COMPLETE;
        workflow_update();
        assert(!workflow_state.moving);
    }
}

enum { STOP_RETRY, LEAVE_AFTER_COMPLETE, LEAVE_BEFORE_COMPLETE };

static void test_material_stop_race_keeps_cooldown(int route)
{
    /* Stop reaches the backend after the move completed: the scoped cancel
     * is rejected, so the heater cleanup it carried never ran. */
    workflow_status_label = (lv_obj_t *)&timer_handle;
    observed.nozzle_temp_cur = 190;
    deneb_material_workflow_init(&workflow_state);
    assert(deneb_material_workflow_prepare(
        &workflow_state, DENEB_MATERIAL_WORKFLOW_OP_CHANGE) == 0);
    deneb_material_workflow_set_target(&workflow_state, 190, 1);
    admitted_operation = 20 + (uint64_t)route;
    observed.manual_state = DENEB_MANUAL_NONE;
    workflow_load_cb(NULL);
    observed.manual_id = admitted_operation;
    observed.manual_state = DENEB_MANUAL_RUNNING;
    workflow_update();
    assert(workflow_state.moving);

    abort_rc = -1;
    sent_gcode[0] = '\0';
    observed.manual_state = DENEB_MANUAL_COMPLETE;
    if (route == LEAVE_BEFORE_COMPLETE) {
        material_workflow_destroy();
        assert(cancel_requested == admitted_operation);
        assert(strcmp(sent_gcode, "M104 S0") == 0);
        assert(!workflow_state.target_sent);
        return;
    }
    workflow_stop_cb(NULL);
    assert(cancel_requested == admitted_operation);
    /* Completion is observed; no unprompted unscoped command is sent. */
    assert(!workflow_state.moving && workflow_state.target_sent);
    assert(workflow_state.accepted_target_temp_c == 190);
    assert(strcmp(shown_status, "material.stop_unconfirmed") == 0);
    assert(sent_gcode[0] == '\0');

    if (route == LEAVE_AFTER_COMPLETE)
        material_workflow_destroy();
    else
        workflow_stop_cb(NULL);
    assert(strcmp(sent_gcode, "M104 S0") == 0);
    assert(!workflow_state.target_sent);
    if (route == STOP_RETRY)
        assert(strcmp(shown_status, "material.stop_unconfirmed") != 0);
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
    test_material_stop_race_keeps_cooldown(STOP_RETRY);
    test_material_stop_race_keeps_cooldown(LEAVE_AFTER_COMPLETE);
    test_material_stop_race_keeps_cooldown(LEAVE_BEFORE_COMPLETE);
    test_jog_rejection_feedback();
    return 0;
}
