/* SPDX-License-Identifier: MPL-2.0 */
#include "manual_motion.h"
#include "gcode_command.h"
#include "json_field.h"
#include "print_macros.h"

#include <stdio.h>
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

int deneb_manual_motion_plan_request(const char *json,
                                     deneb_manual_motion_plan_t *plan)
{
    char action[32];

    if (!json || !plan)
        return DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST;

    if (deneb_json_get_value(json, "action", action, sizeof(action)) < 0) {
        if (strstr(json, "\"home\""))
            snprintf(action, sizeof(action), "%s",
                     DENEB_MANUAL_MOTION_ACTION_HOME);
        else
            return DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST;
    }

    if (deneb_manual_motion_plan_action(action, plan) < 0)
        return DENEB_MANUAL_MOTION_PLAN_UNKNOWN_ACTION;

    return DENEB_MANUAL_MOTION_PLAN_OK;
}
