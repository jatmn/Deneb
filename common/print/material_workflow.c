/* SPDX-License-Identifier: MPL-2.0 */
#include "material_workflow.h"

#include "gcode_command.h"
#include "print_state_rules.h"

#include <string.h>

void deneb_material_workflow_init(deneb_material_workflow_t *wf)
{
    if (!wf)
        return;
    memset(wf, 0, sizeof(*wf));
    wf->operation = DENEB_MATERIAL_WORKFLOW_OP_NONE;
    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_IDLE;
}

int deneb_material_workflow_prepare(deneb_material_workflow_t *wf,
                                    deneb_material_workflow_op_t op)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_IDLE &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_FINAL)
        return -1;
    if (op != DENEB_MATERIAL_WORKFLOW_OP_UNLOAD &&
        op != DENEB_MATERIAL_WORKFLOW_OP_LOAD &&
        op != DENEB_MATERIAL_WORKFLOW_OP_CHANGE)
        return -1;

    wf->operation = op;
    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_PREPARED;
    wf->printing_blocked = 1;
    wf->heating = 0;
    wf->moving = 0;
    wf->target_temp_c = DENEB_MATERIAL_WORKFLOW_DEFAULT_TEMP_C;
    wf->accepted_target_temp_c = 0;
    wf->target_sent = 0;
    return 0;
}

int deneb_material_workflow_start(deneb_material_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_PREPARED)
        return -1;

    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_BUSY;
    wf->accepted_target_temp_c = wf->target_temp_c;
    wf->heating = wf->accepted_target_temp_c > 0;
    wf->target_sent = wf->accepted_target_temp_c > 0;
    return 0;
}

int deneb_material_workflow_set_target(deneb_material_workflow_t *wf,
                                       int target_temp_c, int sent)
{
    if (!wf || target_temp_c < 0)
        return -1;
    if (wf->state == DENEB_MATERIAL_WORKFLOW_STATE_IDLE ||
        wf->state == DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED)
        return -1;

    wf->target_temp_c = target_temp_c;
    wf->accepted_target_temp_c = sent ? target_temp_c : 0;
    wf->target_sent = sent && target_temp_c > 0;
    wf->heating = wf->target_sent;
    if (wf->target_sent &&
        (wf->state == DENEB_MATERIAL_WORKFLOW_STATE_PREPARED ||
         wf->state == DENEB_MATERIAL_WORKFLOW_STATE_FINAL))
        wf->state = DENEB_MATERIAL_WORKFLOW_STATE_BUSY;
    if (target_temp_c == 0 && !wf->moving)
        wf->state = DENEB_MATERIAL_WORKFLOW_STATE_PREPARED;
    return 0;
}

int deneb_material_workflow_edit_target(deneb_material_workflow_t *wf,
                                        int target_temp_c)
{
    if (!wf || target_temp_c < 0)
        return -1;
    if (wf->state == DENEB_MATERIAL_WORKFLOW_STATE_IDLE ||
        wf->state == DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED)
        return -1;

    wf->target_temp_c = target_temp_c;
    if (!wf->target_sent) {
        wf->heating = 0;
        if (target_temp_c == 0 && !wf->moving)
            wf->state = DENEB_MATERIAL_WORKFLOW_STATE_PREPARED;
    }
    return 0;
}

int deneb_material_workflow_begin_move(deneb_material_workflow_t *wf,
                                       deneb_material_workflow_op_t op)
{
    if (!wf)
        return -1;
    if (op != DENEB_MATERIAL_WORKFLOW_OP_UNLOAD &&
        op != DENEB_MATERIAL_WORKFLOW_OP_LOAD &&
        op != DENEB_MATERIAL_WORKFLOW_OP_CHANGE)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_BUSY &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_PREPARED &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_FINAL)
        return -1;
    if (wf->moving)
        return -1;

    wf->operation = op;
    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING;
    wf->heating = 0;
    wf->moving = 1;
    return 0;
}

int deneb_material_workflow_complete_move(deneb_material_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING ||
        !wf->moving)
        return -1;

    wf->operation = DENEB_MATERIAL_WORKFLOW_OP_CHANGE;
    wf->state = (wf->target_sent ? wf->accepted_target_temp_c :
                 wf->target_temp_c) > 0 ?
        DENEB_MATERIAL_WORKFLOW_STATE_BUSY :
        DENEB_MATERIAL_WORKFLOW_STATE_PREPARED;
    wf->heating = wf->target_sent;
    wf->moving = 0;
    return 0;
}

int deneb_material_workflow_advance(deneb_material_workflow_t *wf)
{
    return deneb_material_workflow_begin_move(
        wf, wf ? wf->operation : DENEB_MATERIAL_WORKFLOW_OP_NONE);
}

int deneb_material_workflow_cancel(deneb_material_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_BUSY &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_PREPARED &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING)
        return -1;

    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED;
    wf->heating = 0;
    wf->moving = 0;
    wf->target_temp_c = 0;
    wf->accepted_target_temp_c = 0;
    wf->target_sent = 0;
    return 0;
}

int deneb_material_workflow_finalize(deneb_material_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING &&
        wf->state != DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED)
        return -1;

    wf->operation = DENEB_MATERIAL_WORKFLOW_OP_NONE;
    wf->state = DENEB_MATERIAL_WORKFLOW_STATE_FINAL;
    wf->printing_blocked = 0;
    wf->heating = 0;
    wf->moving = 0;
    wf->target_temp_c = 0;
    wf->accepted_target_temp_c = 0;
    wf->target_sent = 0;
    return 0;
}

int deneb_material_workflow_printing_blocked(const deneb_material_workflow_t *wf)
{
    return wf && wf->printing_blocked;
}

const char *deneb_material_workflow_op_name(deneb_material_workflow_op_t op)
{
    switch (op) {
        case DENEB_MATERIAL_WORKFLOW_OP_UNLOAD:
            return "unload";
        case DENEB_MATERIAL_WORKFLOW_OP_LOAD:
            return "load";
        case DENEB_MATERIAL_WORKFLOW_OP_CHANGE:
            return "change";
        case DENEB_MATERIAL_WORKFLOW_OP_NONE:
        default:
            return "none";
    }
}

const char *deneb_material_workflow_state_name(deneb_material_workflow_state_t state)
{
    switch (state) {
        case DENEB_MATERIAL_WORKFLOW_STATE_IDLE:
            return "idle";
        case DENEB_MATERIAL_WORKFLOW_STATE_PREPARED:
            return "prepared";
        case DENEB_MATERIAL_WORKFLOW_STATE_BUSY:
            return "busy";
        case DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING:
            return "finalizing";
        case DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED:
            return "cancelled";
        case DENEB_MATERIAL_WORKFLOW_STATE_FINAL:
            return "final";
        default:
            return "unknown";
    }
}

void deneb_material_workflow_stop_plan_init(
    deneb_material_workflow_stop_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
}

int deneb_material_workflow_stop_plan(int moving,
                                      deneb_material_workflow_stop_plan_t *plan)
{
    if (!plan)
        return -1;

    deneb_material_workflow_stop_plan_init(plan);
    if (deneb_gcode_format_nozzle_off(plan->nozzle_off,
                                      sizeof(plan->nozzle_off)) < 0)
        return -1;

    plan->stop_gcode = moving ? DENEB_GCODE_STOP_MATERIAL : NULL;
    plan->cooldown_gcode = plan->nozzle_off;
    return 0;
}

deneb_material_workflow_status_t deneb_material_workflow_status(
    int backend_ready,
    int moving,
    int target_sent,
    int target_temp_c,
    int temp_ready)
{
    if (!backend_ready)
        return DENEB_MATERIAL_WORKFLOW_STATUS_BUSY;
    if (moving)
        return DENEB_MATERIAL_WORKFLOW_STATUS_MOVING;
    if (!target_sent && !temp_ready)
        return DENEB_MATERIAL_WORKFLOW_STATUS_SET_TARGET;
    if (target_temp_c == 0)
        return DENEB_MATERIAL_WORKFLOW_STATUS_COOLING;
    if (target_temp_c < DENEB_PRINT_MATERIAL_MIN_MOVE_TEMP_C)
        return DENEB_MATERIAL_WORKFLOW_STATUS_TARGET_TOO_LOW;
    if (temp_ready)
        return DENEB_MATERIAL_WORKFLOW_STATUS_READY_TO_MOVE;
    return DENEB_MATERIAL_WORKFLOW_STATUS_HEATING;
}

deneb_material_workflow_status_t deneb_material_workflow_status_for_state(
    const deneb_material_workflow_t *wf, int backend_ready, int temp_ready)
{
    if (!wf)
        return DENEB_MATERIAL_WORKFLOW_STATUS_BUSY;
    return deneb_material_workflow_status(
        backend_ready, wf->moving, wf->target_sent,
        wf->target_sent ? wf->accepted_target_temp_c : wf->target_temp_c,
        temp_ready);
}
