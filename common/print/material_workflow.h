/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_MATERIAL_WORKFLOW_H
#define DENEB_COMMON_MATERIAL_WORKFLOW_H

#include <stddef.h>

#define DENEB_MATERIAL_WORKFLOW_DEFAULT_TEMP_C 210

typedef enum {
    DENEB_MATERIAL_WORKFLOW_OP_NONE = 0,
    DENEB_MATERIAL_WORKFLOW_OP_UNLOAD,
    DENEB_MATERIAL_WORKFLOW_OP_LOAD,
    DENEB_MATERIAL_WORKFLOW_OP_CHANGE
} deneb_material_workflow_op_t;

typedef enum {
    DENEB_MATERIAL_WORKFLOW_STATE_IDLE = 0,
    DENEB_MATERIAL_WORKFLOW_STATE_PREPARED,
    DENEB_MATERIAL_WORKFLOW_STATE_BUSY,
    DENEB_MATERIAL_WORKFLOW_STATE_FINALIZING,
    DENEB_MATERIAL_WORKFLOW_STATE_CANCELLED,
    DENEB_MATERIAL_WORKFLOW_STATE_FINAL
} deneb_material_workflow_state_t;

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
    deneb_material_workflow_op_t operation;
    deneb_material_workflow_state_t state;
    int printing_blocked;
    int heating;
    int moving;
    int target_temp_c;
    int accepted_target_temp_c;
    int target_sent;
} deneb_material_workflow_t;

typedef struct {
    const char *stop_gcode;
    const char *cooldown_gcode;
    char nozzle_off[32];
} deneb_material_workflow_stop_plan_t;

void deneb_material_workflow_init(deneb_material_workflow_t *wf);
int deneb_material_workflow_prepare(deneb_material_workflow_t *wf,
                                    deneb_material_workflow_op_t op);
int deneb_material_workflow_start(deneb_material_workflow_t *wf);
int deneb_material_workflow_set_target(deneb_material_workflow_t *wf,
                                       int target_temp_c, int sent);
int deneb_material_workflow_edit_target(deneb_material_workflow_t *wf,
                                        int target_temp_c);
int deneb_material_workflow_begin_move(deneb_material_workflow_t *wf,
                                       deneb_material_workflow_op_t op);
int deneb_material_workflow_complete_move(deneb_material_workflow_t *wf);
int deneb_material_workflow_advance(deneb_material_workflow_t *wf);
int deneb_material_workflow_cancel(deneb_material_workflow_t *wf);
int deneb_material_workflow_finalize(deneb_material_workflow_t *wf);
int deneb_material_workflow_printing_blocked(const deneb_material_workflow_t *wf);
const char *deneb_material_workflow_op_name(deneb_material_workflow_op_t op);
const char *deneb_material_workflow_state_name(deneb_material_workflow_state_t state);

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
deneb_material_workflow_status_t deneb_material_workflow_status_for_state(
    const deneb_material_workflow_t *wf, int backend_ready, int temp_ready);

#endif
