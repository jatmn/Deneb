/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_MATERIAL_WORKFLOW_H
#define DENEB_COMMON_MATERIAL_WORKFLOW_H

#define DENEB_MATERIAL_WORKFLOW_DEFAULT_TEMP_C 210

typedef enum {
    DENEB_MATERIAL_WORKFLOW_STATUS_BUSY = 0,
    DENEB_MATERIAL_WORKFLOW_STATUS_MOVING,
    DENEB_MATERIAL_WORKFLOW_STATUS_SET_TARGET,
    DENEB_MATERIAL_WORKFLOW_STATUS_COOLING,
    DENEB_MATERIAL_WORKFLOW_STATUS_TARGET_TOO_LOW,
    DENEB_MATERIAL_WORKFLOW_STATUS_READY_TO_MOVE,
    DENEB_MATERIAL_WORKFLOW_STATUS_HEATING
} deneb_material_workflow_status_t;

typedef struct {
    const char *stop_gcode;
    const char *cooldown_gcode;
    char nozzle_off[32];
} deneb_material_workflow_stop_plan_t;

void deneb_material_workflow_stop_plan_init(
    deneb_material_workflow_stop_plan_t *plan);
int deneb_material_workflow_stop_plan(int moving,
                                      deneb_material_workflow_stop_plan_t *plan);
deneb_material_workflow_status_t deneb_material_workflow_status(
    int backend_ready,
    int moving,
    int target_sent,
    int target_temp_c,
    int temp_ready);

#endif
