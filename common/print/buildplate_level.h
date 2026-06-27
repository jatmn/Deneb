/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_BUILDPLATE_LEVEL_H
#define DENEB_COMMON_BUILDPLATE_LEVEL_H

#include <stddef.h>

typedef enum {
    DENEB_BUILDPLATE_LEVEL_STEP_1 = 0,
    DENEB_BUILDPLATE_LEVEL_STEP_2,
    DENEB_BUILDPLATE_LEVEL_STEP_3,
    DENEB_BUILDPLATE_LEVEL_STEP_4,
    DENEB_BUILDPLATE_LEVEL_STEP_FINISH
} deneb_buildplate_level_step_t;

typedef enum {
    DENEB_BUILDPLATE_LEVEL_STATE_NONE = 0,
    DENEB_BUILDPLATE_LEVEL_STATE_PREPARED,
    DENEB_BUILDPLATE_LEVEL_STATE_MOVING,
    DENEB_BUILDPLATE_LEVEL_STATE_AT_TARGET,
    DENEB_BUILDPLATE_LEVEL_STATE_FINAL,
    DENEB_BUILDPLATE_LEVEL_STATE_CANCELLED
} deneb_buildplate_level_state_t;

typedef struct {
    deneb_buildplate_level_state_t state;
    deneb_buildplate_level_step_t current_step;
    int step_count;
    int moving;
} deneb_buildplate_level_workflow_t;

typedef struct {
    const char *macro;
} deneb_buildplate_level_plan_t;

void deneb_buildplate_level_workflow_init(
    deneb_buildplate_level_workflow_t *wf);
int deneb_buildplate_level_workflow_prepare(
    deneb_buildplate_level_workflow_t *wf);
int deneb_buildplate_level_workflow_start(
    deneb_buildplate_level_workflow_t *wf);
int deneb_buildplate_level_workflow_advance(
    deneb_buildplate_level_workflow_t *wf,
    deneb_buildplate_level_step_t next_step);
int deneb_buildplate_level_workflow_complete_move(
    deneb_buildplate_level_workflow_t *wf);
int deneb_buildplate_level_workflow_cancel(
    deneb_buildplate_level_workflow_t *wf);
int deneb_buildplate_level_workflow_next_step(
    const deneb_buildplate_level_workflow_t *wf,
    deneb_buildplate_level_step_t *out_step);
const char *deneb_buildplate_level_state_name(
    deneb_buildplate_level_state_t state);

void deneb_buildplate_level_plan_init(deneb_buildplate_level_plan_t *plan);
int deneb_buildplate_level_plan_step(deneb_buildplate_level_step_t step,
                                     deneb_buildplate_level_plan_t *plan);

#endif
