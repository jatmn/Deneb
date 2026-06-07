/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_policy.h"
#include "gcode_command.h"

#include <string.h>

static void policy_clear(deneb_motion_policy_t *policy)
{
    memset(policy, 0, sizeof(*policy));
}

static void policy_add(deneb_motion_policy_t *policy, const char *command)
{
    if (policy->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    strncpy(policy->commands[policy->count], command,
            sizeof(policy->commands[policy->count]) - 1);
    policy->count++;
}

void deneb_motion_policy_abort(deneb_motion_policy_t *policy)
{
    char nozzle_off[32] = "";
    char bed_off[32] = "";

    policy_clear(policy);
    deneb_gcode_format_nozzle_off(nozzle_off, sizeof(nozzle_off));
    deneb_gcode_format_bed_off(bed_off, sizeof(bed_off));
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, nozzle_off);
    policy_add(policy, bed_off);
    policy_add(policy, DENEB_GCODE_FAN_OFF);
    policy_add(policy, DENEB_GCODE_DISABLE_EXTRUDER_STEPPER);
}

void deneb_motion_policy_finish(deneb_motion_policy_t *policy)
{
    char nozzle_off[32] = "";
    char bed_off[32] = "";

    policy_clear(policy);
    deneb_gcode_format_nozzle_off(nozzle_off, sizeof(nozzle_off));
    deneb_gcode_format_bed_off(bed_off, sizeof(bed_off));
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, nozzle_off);
    policy_add(policy, bed_off);
    policy_add(policy, DENEB_GCODE_FAN_OFF);
    policy_add(policy, DENEB_GCODE_DISABLE_ALL_STEPPERS);
}

int deneb_motion_policy_contains_xy_home(const deneb_motion_policy_t *policy)
{
    if (!policy)
        return 0;
    for (size_t i = 0; i < policy->count; i++) {
        const char *cmd = policy->commands[i];
        if (strncmp(cmd, "G28", 3) == 0 &&
            (strchr(cmd, 'X') || strchr(cmd, 'Y') || strlen(cmd) == 3))
            return 1;
    }
    return 0;
}
