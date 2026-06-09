/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_POLICY_H
#define DENEB_PRINTSVC_MOTION_POLICY_H

#include "config.h"

#include <stddef.h>

typedef struct {
    char commands[DENEB_PRINTSVC_MAX_COMMANDS][DENEB_PRINTSVC_MAX_GCODE_LINE];
    size_t count;
} deneb_motion_policy_t;

void deneb_motion_policy_abort(deneb_motion_policy_t *policy);
void deneb_motion_policy_finish(deneb_motion_policy_t *policy);
void deneb_motion_policy_pause(deneb_motion_policy_t *policy,
                               float x, float y, float z);
void deneb_motion_policy_resume(deneb_motion_policy_t *policy,
                                float x, float y, float z, float e,
                                float r0, float nozzle_setpoint);
int deneb_motion_policy_contains_xy_home(const deneb_motion_policy_t *policy);

#endif
