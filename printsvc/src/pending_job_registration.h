/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_PENDING_JOB_REGISTRATION_H
#define DENEB_PRINTSVC_PENDING_JOB_REGISTRATION_H

#include "pending_job.h"

typedef struct {
    deneb_pending_job_t job;
    int change_count;
    int should_start_immediately;
} deneb_pending_job_registration_t;

void deneb_pending_job_registration_init(
    deneb_pending_job_registration_t *registration);
int deneb_pending_job_registration_prepare(
    const char *path,
    long long tracker_seed,
    deneb_pending_job_registration_t *registration);
int deneb_pending_job_registration_write_default(
    const deneb_pending_job_registration_t *registration);

#endif
