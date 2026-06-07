/* SPDX-License-Identifier: MPL-2.0 */
#include "pause_resume.h"
#include "print_control.h"

#include <stdio.h>

static void set_phase(deneb_status_t *status, deneb_print_phase_t phase)
{
    status->state = deneb_print_control_state_for_phase(phase);
    snprintf(status->req, sizeof(status->req), "%s",
             deneb_print_control_req_for_phase(phase));
}

int deneb_pause_resume_pause(deneb_status_t *status)
{
    if (!status)
        return -1;

    switch (status->state) {
        case DENEB_PRINT_STATE_PREPARING:
        case DENEB_PRINT_STATE_PRINTING:
            set_phase(status, DENEB_PRINT_PHASE_PAUSED);
            return 1;
        case DENEB_PRINT_STATE_PAUSED:
            set_phase(status, DENEB_PRINT_PHASE_PAUSED);
            return 0;
        case DENEB_PRINT_STATE_IDLE:
        case DENEB_PRINT_STATE_ABORTING:
        case DENEB_PRINT_STATE_COMPLETE:
        case DENEB_PRINT_STATE_ERROR:
        default:
            return -1;
    }
}

int deneb_pause_resume_resume(deneb_status_t *status, int heater_wait_active)
{
    if (!status || status->state != DENEB_PRINT_STATE_PAUSED)
        return -1;

    set_phase(status, heater_wait_active ? DENEB_PRINT_PHASE_PREPARING :
                                          DENEB_PRINT_PHASE_PRINTING);
    return 1;
}
