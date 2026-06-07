/* SPDX-License-Identifier: MPL-2.0 */
#include "job_lifecycle.h"
#include "print_control.h"
#include "print_state_rules.h"

#include <stdio.h>

static void set_phase(deneb_status_t *status, deneb_print_phase_t phase)
{
    status->state = deneb_print_control_state_for_phase(phase);
    snprintf(status->req, sizeof(status->req), "%s",
             deneb_print_control_req_for_phase(phase));
}

void deneb_job_lifecycle_start(deneb_status_t *status,
                               const char *file,
                               const char *source,
                               const char *uuid,
                               float bed_target,
                               float head_target)
{
    if (!status)
        return;

    set_phase(status, DENEB_PRINT_PHASE_PREPARING);
    snprintf(status->file, sizeof(status->file), "%s",
             file && file[0] ? file : DENEB_PRINT_NONE_VALUE);
    snprintf(status->source, sizeof(status->source), "%s",
             source && source[0] ? source : DENEB_PRINT_USB_JOB_SOURCE);
    snprintf(status->uuid, sizeof(status->uuid), "%s", uuid ? uuid : "");
    if (bed_target > 0.0f)
        status->bed_t_set = bed_target;
    if (head_target > 0.0f)
        status->head_t_set = head_target;
}

void deneb_job_lifecycle_streaming(deneb_status_t *status)
{
    if (!status)
        return;
    set_phase(status, DENEB_PRINT_PHASE_PRINTING);
}

void deneb_job_lifecycle_complete(deneb_status_t *status)
{
    if (!status)
        return;
    set_phase(status, DENEB_PRINT_PHASE_COMPLETE);
}

void deneb_job_lifecycle_abort(deneb_status_t *status)
{
    if (!status)
        return;

    set_phase(status, DENEB_PRINT_PHASE_IDLE);
    snprintf(status->file, sizeof(status->file), "%s", DENEB_PRINT_NONE_VALUE);
    status->time_total = 0;
    status->time_left = 0;
}

void deneb_job_lifecycle_error(deneb_status_t *status, deneb_error_t error)
{
    if (!status)
        return;

    set_phase(status, DENEB_PRINT_PHASE_ERROR);
    status->fault = true;
    status->error = error;
}
