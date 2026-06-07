/* SPDX-License-Identifier: MPL-2.0 */
#include "print_control.h"
#include "command_format.h"

deneb_print_phase_t deneb_print_control_phase_from_state(deneb_print_state_t state)
{
    switch (state) {
        case DENEB_PRINT_STATE_PREPARING:
            return DENEB_PRINT_PHASE_PREPARING;
        case DENEB_PRINT_STATE_PRINTING:
            return DENEB_PRINT_PHASE_PRINTING;
        case DENEB_PRINT_STATE_PAUSED:
            return DENEB_PRINT_PHASE_PAUSED;
        case DENEB_PRINT_STATE_ABORTING:
            return DENEB_PRINT_PHASE_ABORTING;
        case DENEB_PRINT_STATE_COMPLETE:
            return DENEB_PRINT_PHASE_COMPLETE;
        case DENEB_PRINT_STATE_ERROR:
            return DENEB_PRINT_PHASE_ERROR;
        case DENEB_PRINT_STATE_IDLE:
        default:
            return DENEB_PRINT_PHASE_IDLE;
    }
}

const char *deneb_print_control_phase_name(deneb_print_phase_t phase)
{
    switch (phase) {
        case DENEB_PRINT_PHASE_PREPARING:
            return "pre_print";
        case DENEB_PRINT_PHASE_PRINTING:
            return "printing";
        case DENEB_PRINT_PHASE_PAUSED:
            return "paused";
        case DENEB_PRINT_PHASE_ABORTING:
            return "aborting";
        case DENEB_PRINT_PHASE_COMPLETE:
            return "finished";
        case DENEB_PRINT_PHASE_ERROR:
            return "error";
        case DENEB_PRINT_PHASE_IDLE:
        default:
            return "idle";
    }
}

const char *deneb_print_control_req_for_phase(deneb_print_phase_t phase)
{
    switch (phase) {
        case DENEB_PRINT_PHASE_PREPARING:
            return "PREPARE";
        case DENEB_PRINT_PHASE_PRINTING:
            return DENEB_COMMAND_VERB_JOB;
        case DENEB_PRINT_PHASE_PAUSED:
            return "Paused";
        case DENEB_PRINT_PHASE_ABORTING:
            return DENEB_COMMAND_VERB_ABORT;
        case DENEB_PRINT_PHASE_COMPLETE:
            return "Complete";
        case DENEB_PRINT_PHASE_ERROR:
            return "Error";
        case DENEB_PRINT_PHASE_IDLE:
        default:
            return "Idle";
    }
}

int deneb_print_control_phase_active(deneb_print_phase_t phase)
{
    return phase == DENEB_PRINT_PHASE_PREPARING ||
           phase == DENEB_PRINT_PHASE_PRINTING ||
           phase == DENEB_PRINT_PHASE_PAUSED ||
           phase == DENEB_PRINT_PHASE_ABORTING;
}

int deneb_print_control_phase_stop_allowed(deneb_print_phase_t phase)
{
    return phase == DENEB_PRINT_PHASE_PREPARING ||
           phase == DENEB_PRINT_PHASE_PRINTING ||
           phase == DENEB_PRINT_PHASE_PAUSED;
}

const char *deneb_print_control_action_command(deneb_print_action_t action)
{
    switch (action) {
        case DENEB_PRINT_ACTION_PAUSE:
            return DENEB_COMMAND_VERB_PAUSE;
        case DENEB_PRINT_ACTION_RESUME:
            return DENEB_COMMAND_VERB_RESUME;
        case DENEB_PRINT_ACTION_ABORT:
            return DENEB_COMMAND_VERB_ABORT;
        case DENEB_PRINT_ACTION_START:
            return DENEB_COMMAND_VERB_JOB;
        case DENEB_PRINT_ACTION_NONE:
        default:
            return "";
    }
}
