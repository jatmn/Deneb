/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_BUILDPLATE_LEVEL_H
#define DENEB_COMMON_BUILDPLATE_LEVEL_H

typedef enum {
    DENEB_BUILDPLATE_LEVEL_STEP_1 = 0,
    DENEB_BUILDPLATE_LEVEL_STEP_2,
    DENEB_BUILDPLATE_LEVEL_STEP_3,
    DENEB_BUILDPLATE_LEVEL_STEP_4,
    DENEB_BUILDPLATE_LEVEL_STEP_FINISH
} deneb_buildplate_level_step_t;

typedef struct {
    const char *macro;
} deneb_buildplate_level_plan_t;

void deneb_buildplate_level_plan_init(deneb_buildplate_level_plan_t *plan);
int deneb_buildplate_level_plan_step(deneb_buildplate_level_step_t step,
                                     deneb_buildplate_level_plan_t *plan);

#endif
