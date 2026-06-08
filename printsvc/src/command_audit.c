/* SPDX-License-Identifier: MPL-2.0 */
#include "command_audit.h"

#include "diagnostics_log.h"
#include "runtime_diagnostics.h"

#include <time.h>

static long long audit_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

unsigned int deneb_command_audit_elapsed_ms(long long start_ms,
                                            long long end_ms)
{
    long long elapsed_ms = end_ms - start_ms;

    if (elapsed_ms < 0)
        elapsed_ms = 0;
    if (elapsed_ms > 60000)
        elapsed_ms = 60000;
    return (unsigned int)elapsed_ms;
}

int deneb_command_audit_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz,
                            deneb_command_audit_handler_t handler,
                            void *handler_ctx)
{
    long long start_ms;
    int rc;

    if (!svc || !cmd || !reply || reply_sz == 0 || !handler)
        return -1;

    start_ms = audit_monotonic_ms();
    rc = handler(handler_ctx, cmd, reply, reply_sz);
    svc->status.command_latency_ms =
        deneb_command_audit_elapsed_ms(start_ms, audit_monotonic_ms());

    deneb_runtime_diagnostics_refresh(
        &svc->status, &svc->flow, svc->job_active,
        (unsigned int)svc->job_stream.line_number,
        svc->planner_starvation_count);
    deneb_diagnostics_log_command(cmd, rc, svc->status.command_latency_ms);
    deneb_diagnostics_log_status(&svc->status, 1);
    return rc;
}
