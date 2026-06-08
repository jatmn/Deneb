/* SPDX-License-Identifier: MPL-2.0 */
#include "material_workflow.h"

#include "gcode_command.h"
#include "print_state_rules.h"

#include <string.h>

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
