/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_MANUAL_MOTION_H
#define DENEB_COMMON_MANUAL_MOTION_H

#define DENEB_MANUAL_MOTION_ACTION_HOME "home"
#define DENEB_MANUAL_MOTION_ACTION_Z_HOME "z_home"
#define DENEB_MANUAL_MOTION_ACTION_BED_UP "bed_up"
#define DENEB_MANUAL_MOTION_ACTION_BED_DOWN "bed_down"

typedef enum {
    DENEB_MANUAL_MOTION_NONE = 0,
    DENEB_MANUAL_MOTION_GCODE,
    DENEB_MANUAL_MOTION_MACRO
} deneb_manual_motion_kind_t;

typedef struct {
    deneb_manual_motion_kind_t kind;
    const char *command;
} deneb_manual_motion_plan_t;

void deneb_manual_motion_plan_init(deneb_manual_motion_plan_t *plan);
int deneb_manual_motion_plan_action(const char *action,
                                    deneb_manual_motion_plan_t *plan);

#endif
