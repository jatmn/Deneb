/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PENDING_JOB_DISPATCH_H
#define DENEB_COMMON_PENDING_JOB_DISPATCH_H

#include "print_job_file.h"

typedef struct {
    void *ctx;
    int (*start_allowed)(void *ctx);
    int (*send_abort)(void *ctx);
    int (*send_job)(void *ctx, const deneb_print_job_start_plan_t *plan);
} deneb_pending_job_dispatch_ops_t;

int deneb_pending_job_dispatch_default(
    const char *instruction,
    const deneb_pending_job_dispatch_ops_t *ops);
int deneb_pending_job_dispatch_from_path(
    const char *pending_path,
    const char *instruction,
    const deneb_pending_job_dispatch_ops_t *ops);

#endif
