/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_PRINT_CONTROL_H
#define DENEB_PRINTSVC_PRINT_CONTROL_H

#include "status.h"

typedef enum {
    DENEB_PRINT_PHASE_IDLE = 0,
    DENEB_PRINT_PHASE_PREPARING,
    DENEB_PRINT_PHASE_PRINTING,
    DENEB_PRINT_PHASE_PAUSED,
    DENEB_PRINT_PHASE_ABORTING,
    DENEB_PRINT_PHASE_COMPLETE,
    DENEB_PRINT_PHASE_ERROR
} deneb_print_phase_t;

typedef enum {
    DENEB_PRINT_ACTION_NONE = 0,
    DENEB_PRINT_ACTION_START,
    DENEB_PRINT_ACTION_PAUSE,
    DENEB_PRINT_ACTION_RESUME,
    DENEB_PRINT_ACTION_ABORT
} deneb_print_action_t;

deneb_print_phase_t deneb_print_control_phase_from_state(deneb_print_state_t state);
deneb_print_state_t deneb_print_control_state_for_phase(deneb_print_phase_t phase);
const char *deneb_print_control_phase_name(deneb_print_phase_t phase);
const char *deneb_print_control_req_for_phase(deneb_print_phase_t phase);
int deneb_print_control_phase_active(deneb_print_phase_t phase);
int deneb_print_control_phase_stop_allowed(deneb_print_phase_t phase);
const char *deneb_print_control_action_command(deneb_print_action_t action);

#endif
