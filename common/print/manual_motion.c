/* SPDX-License-Identifier: MPL-2.0 */
#include "manual_motion.h"
#include "gcode_command.h"
#include "print_macros.h"

#include <string.h>

void deneb_manual_motion_plan_init(deneb_manual_motion_plan_t *plan)
{
    if (!plan)
        return;
    plan->kind = DENEB_MANUAL_MOTION_NONE;
    plan->command = NULL;
}

int deneb_manual_motion_plan_action(const char *action,
                                    deneb_manual_motion_plan_t *plan)
{
    if (!action || !plan)
        return -1;

    deneb_manual_motion_plan_init(plan);
    if (strcmp(action, DENEB_MANUAL_MOTION_ACTION_HOME) == 0) {
        plan->kind = DENEB_MANUAL_MOTION_MACRO;
        plan->command = DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD;
        return 0;
    }
    if (strcmp(action, DENEB_MANUAL_MOTION_ACTION_Z_HOME) == 0) {
        plan->kind = DENEB_MANUAL_MOTION_GCODE;
        plan->command = DENEB_GCODE_HOME_Z;
        return 0;
    }
    if (strcmp(action, DENEB_MANUAL_MOTION_ACTION_BED_UP) == 0) {
        plan->kind = DENEB_MANUAL_MOTION_MACRO;
        plan->command = DENEB_PRINT_MACRO_MOVE_BUILDPLATE_UP;
        return 0;
    }
    if (strcmp(action, DENEB_MANUAL_MOTION_ACTION_BED_DOWN) == 0) {
        plan->kind = DENEB_MANUAL_MOTION_MACRO;
        plan->command = DENEB_PRINT_MACRO_MOVE_BUILDPLATE_DOWN;
        return 0;
    }
    return -1;
}
