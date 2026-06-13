/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_JOB_LIFECYCLE_H
#define DENEB_PRINTSVC_JOB_LIFECYCLE_H

#include "error_map.h"
#include "status.h"

void deneb_job_lifecycle_start(deneb_status_t *status,
                               const char *file,
                               const char *source,
                               const char *uuid,
                               const char *cloud_job_id,
                               float bed_target,
                               float head_target);
void deneb_job_lifecycle_streaming(deneb_status_t *status);
void deneb_job_lifecycle_complete(deneb_status_t *status);
void deneb_job_lifecycle_aborting(deneb_status_t *status);
void deneb_job_lifecycle_abort(deneb_status_t *status);
void deneb_job_lifecycle_error(deneb_status_t *status, deneb_error_t error);

#endif
