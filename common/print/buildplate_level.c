/* SPDX-License-Identifier: MPL-2.0 */
#include "buildplate_level.h"

#include "print_macros.h"

#include <stddef.h>

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
