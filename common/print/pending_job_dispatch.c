/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job_dispatch.h"

#include "pending_job_file.h"

static int dispatch_start(const deneb_pending_job_action_plan_t *pending_plan,
                          const deneb_pending_job_dispatch_ops_t *ops)
{
    deneb_print_job_start_plan_t start_plan;

    if (!ops || !ops->start_allowed || !ops->send_job ||
        !ops->start_allowed(ops->ctx))
        return -1;

    if (deneb_print_job_start_plan_prepare(
            pending_plan->path, pending_plan->source, pending_plan->uuid,
            pending_plan->cloud_job_id, 0.0f, 0.0f, &start_plan) < 0)
        return -1;

    return ops->send_job(ops->ctx, &start_plan);
}

int deneb_pending_job_dispatch_from_path(
    const char *pending_path,
    const char *instruction,
    const deneb_pending_job_dispatch_ops_t *ops)
{
    deneb_pending_job_file_t job;
    deneb_pending_job_action_plan_t pending_plan;
    const char *path = pending_path && pending_path[0] ?
        pending_path : DENEB_PENDING_JOB_PATH;

    if (!ops || !ops->send_abort || !ops->send_job)
        return -1;

    if (deneb_pending_job_file_load(path, &job) != 0 ||
        deneb_pending_job_file_plan_action(&job, instruction,
                                           &pending_plan) != 0)
        return -1;

    if (pending_plan.kind == DENEB_PENDING_JOB_ACTION_ABORT) {
        if (ops->send_abort(ops->ctx) != 0)
            return -1;
    } else if (pending_plan.kind == DENEB_PENDING_JOB_ACTION_START) {
        if (dispatch_start(&pending_plan, ops) != 0)
            return -1;
    } else {
        return -1;
    }

    return deneb_pending_job_file_finish_action(path, &pending_plan);
}

int deneb_pending_job_dispatch_default(
    const char *instruction,
    const deneb_pending_job_dispatch_ops_t *ops)
{
    return deneb_pending_job_dispatch_from_path(DENEB_PENDING_JOB_PATH,
                                               instruction, ops);
}
