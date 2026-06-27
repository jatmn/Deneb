/* SPDX-License-Identifier: MPL-2.0 */
#include "buildplate_level.h"

#include "print_macros.h"

#include <stddef.h>

void deneb_buildplate_level_workflow_init(
    deneb_buildplate_level_workflow_t *wf)
{
    if (!wf)
        return;
    wf->state = DENEB_BUILDPLATE_LEVEL_STATE_NONE;
    wf->current_step = DENEB_BUILDPLATE_LEVEL_STEP_1;
    wf->step_count = 0;
    wf->moving = 0;
}

int deneb_buildplate_level_workflow_prepare(
    deneb_buildplate_level_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_BUILDPLATE_LEVEL_STATE_NONE &&
        wf->state != DENEB_BUILDPLATE_LEVEL_STATE_FINAL &&
        wf->state != DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED)
        return -1;

    wf->state = DENEB_BUILDPLATE_LEVEL_STATE_PREPARED;
    wf->current_step = DENEB_BUILDPLATE_LEVEL_STEP_1;
    wf->step_count = 0;
    wf->moving = 0;
    return 0;
}

int deneb_buildplate_level_workflow_start(
    deneb_buildplate_level_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_BUILDPLATE_LEVEL_STATE_PREPARED)
        return -1;

    wf->state = DENEB_BUILDPLATE_LEVEL_STATE_MOVING;
    wf->current_step = DENEB_BUILDPLATE_LEVEL_STEP_1;
    wf->moving = 1;
    return 0;
}

int deneb_buildplate_level_workflow_advance(
    deneb_buildplate_level_workflow_t *wf,
    deneb_buildplate_level_step_t next_step)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_BUILDPLATE_LEVEL_STATE_MOVING &&
        wf->state != DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET)
        return -1;
    if (next_step < DENEB_BUILDPLATE_LEVEL_STEP_1 ||
        next_step > DENEB_BUILDPLATE_LEVEL_STEP_FINISH)
        return -1;

    wf->state = DENEB_BUILDPLATE_LEVEL_STATE_MOVING;
    wf->current_step = next_step;
    wf->moving = 1;
    return 0;
}

int deneb_buildplate_level_workflow_cancel(
    deneb_buildplate_level_workflow_t *wf)
{
    if (!wf)
        return -1;
    if (wf->state != DENEB_BUILDPLATE_LEVEL_STATE_PREPARED &&
        wf->state != DENEB_BUILDPLATE_LEVEL_STATE_MOVING &&
        wf->state != DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET)
        return -1;

    wf->state = DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED;
    wf->moving = 0;
    return 0;
}

int deneb_buildplate_level_workflow_next_step(
    const deneb_buildplate_level_workflow_t *wf,
    deneb_buildplate_level_step_t *out_step)
{
    if (!wf || !out_step)
        return -1;

    if (wf->state == DENEB_BUILDPLATE_LEVEL_STATE_PREPARED) {
        *out_step = DENEB_BUILDPLATE_LEVEL_STEP_1;
        return 0;
    }

    if (wf->state == DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET) {
        if (wf->current_step == DENEB_BUILDPLATE_LEVEL_STEP_FINISH)
            return -1;
        *out_step = (deneb_buildplate_level_step_t)((int)wf->current_step + 1);
        return 0;
    }

    return -1;
}

const char *deneb_buildplate_level_state_name(
    deneb_buildplate_level_state_t state)
{
    switch (state) {
        case DENEB_BUILDPLATE_LEVEL_STATE_NONE:
            return "none";
        case DENEB_BUILDPLATE_LEVEL_STATE_PREPARED:
            return "prepared";
        case DENEB_BUILDPLATE_LEVEL_STATE_MOVING:
            return "moving";
        case DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET:
            return "at_target";
        case DENEB_BUILDPLATE_LEVEL_STATE_FINAL:
            return "final";
        case DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED:
            return "cancelled";
        default:
            return "unknown";
    }
}

void deneb_buildplate_level_plan_init(deneb_buildplate_level_plan_t *plan)
{
    if (!plan)
        return;
    plan->macro = NULL;
}

int deneb_buildplate_level_plan_step(deneb_buildplate_level_step_t step,
                                     deneb_buildplate_level_plan_t *plan)
{
    if (!plan)
        return -1;

    deneb_buildplate_level_plan_init(plan);
    switch (step) {
        case DENEB_BUILDPLATE_LEVEL_STEP_1:
            plan->macro = DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP1;
            return 0;
        case DENEB_BUILDPLATE_LEVEL_STEP_2:
            plan->macro = DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP2;
            return 0;
        case DENEB_BUILDPLATE_LEVEL_STEP_3:
            plan->macro = DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP3;
            return 0;
        case DENEB_BUILDPLATE_LEVEL_STEP_4:
            plan->macro = DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP4;
            return 0;
        case DENEB_BUILDPLATE_LEVEL_STEP_FINISH:
            plan->macro = DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_FINISH;
            return 0;
        default:
            return -1;
    }
}
