/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_policy.h"
#include "gcode_command.h"

#include <stdio.h>
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

static float clamp_position(char axis, float value)
{
    if (value < 0.0f)
        return 0.0f;
    if (!deneb_gcode_valid_position_value(axis, value)) {
        switch (axis) {
            case 'X':
                return DENEB_GCODE_MAX_POSITION_X_MM;
            case 'Y':
                return DENEB_GCODE_MAX_POSITION_Y_MM;
            case 'Z':
                return DENEB_GCODE_MAX_POSITION_Z_MM;
            default:
                return value;
        }
    }
    return value;
}

static void policy_addf(deneb_motion_policy_t *policy, const char *fmt,
                        float a)
{
    if (policy->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    snprintf(policy->commands[policy->count],
             sizeof(policy->commands[policy->count]), fmt, a);
    policy->count++;
}

static void policy_addff(deneb_motion_policy_t *policy, const char *fmt,
                         float a, float b)
{
    if (policy->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    snprintf(policy->commands[policy->count],
             sizeof(policy->commands[policy->count]), fmt, a, b);
    policy->count++;
}

void deneb_motion_policy_abort(deneb_motion_policy_t *policy)
{
    policy_clear(policy);
    policy_add(policy, DENEB_GCODE_RELATIVE_MODE);
    policy_add(policy, "G1 X20 Y20 E-6.5 F9000");
    policy_add(policy, "G1 Z3");
    policy_add(policy, DENEB_GCODE_ABSOLUTE_MODE);
    policy_add(policy, "G10 S-16.5");
    policy_add(policy, "G28 X Y");
    policy_add(policy, DENEB_GCODE_HOME_Z);
    policy_add(policy, "M104 S0");
    policy_add(policy, "M140 S0");
    policy_add(policy, DENEB_GCODE_FAN_OFF);
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, DENEB_GCODE_DISABLE_ALL_STEPPERS);
}

void deneb_motion_policy_finish(deneb_motion_policy_t *policy)
{
    policy_clear(policy);
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, DENEB_GCODE_RELATIVE_MODE);
    policy_add(policy, "G1 Z3");
    policy_add(policy, DENEB_GCODE_ABSOLUTE_MODE);
    policy_add(policy, "G28 X Y");
    policy_add(policy, DENEB_GCODE_HOME_Z);
    policy_add(policy, "M104 S0");
    policy_add(policy, "M140 S0");
    policy_add(policy, DENEB_GCODE_FAN_OFF);
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, DENEB_GCODE_DISABLE_ALL_STEPPERS);
}

void deneb_motion_policy_pause(deneb_motion_policy_t *policy,
                               float x, float y, float z)
{
    float wipe_x;
    float lift_z;

    policy_clear(policy);
    x = clamp_position('X', x);
    y = clamp_position('Y', y);
    z = clamp_position('Z', z);
    wipe_x = clamp_position('X', x + (x <= 100.0f ? 50.0f : -50.0f));
    lift_z = clamp_position('Z', z <= 200.0f ? z + 10.0f : z);

    policy_add(policy, "M83");
    policy_add(policy, "G0 F9000");
    policy_addf(policy, "G1 E-6.5 X%.3g", wipe_x);
    policy_addf(policy, "G0 Z%.3g", lift_z);
    policy_add(policy, "G0 X5 Y10");
    policy_add(policy, "M82");
    policy_add(policy, "G10 S-16.5 F1500");
    policy_addf(policy, "G0 Z%.3g F9000", DENEB_GCODE_MAX_POSITION_Z_MM);
    policy_add(policy, DENEB_GCODE_WAIT_FOR_MOVES);
    policy_add(policy, "M104 S0");
}

void deneb_motion_policy_resume(deneb_motion_policy_t *policy,
                                float x, float y, float z, float e,
                                float r0, float nozzle_setpoint)
{
    int heated_resume;

    policy_clear(policy);
    x = clamp_position('X', x);
    y = clamp_position('Y', y);
    z = clamp_position('Z', z);
    if (nozzle_setpoint < 0.0f)
        nozzle_setpoint = 0.0f;
    if (nozzle_setpoint > DENEB_GCODE_MAX_NOZZLE_TEMP_C)
        nozzle_setpoint = DENEB_GCODE_MAX_NOZZLE_TEMP_C;
    heated_resume = nozzle_setpoint > 0.0f;

    if (heated_resume) {
        policy_addf(policy, "M104 S%.3g", nozzle_setpoint);
        policy_addf(policy, "M109 S%.3g", nozzle_setpoint);
        policy_add(policy, "M83");
        policy_add(policy, "G0 E10 F1500");
    }
    policy_add(policy, "G0 F9000");
    policy_addff(policy, "G0 X%.3g Y%.3g", x, y);
    policy_addf(policy, "G0 Z%.3g", z);
    if (heated_resume) {
        policy_add(policy, "G0 E6.5 F1500");
        policy_add(policy, "M82");
        policy_addf(policy, "G10 S%.3g", r0);
        policy_addf(policy, "G92 E%.3g", e);
    }
    policy_add(policy, "M105");
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
