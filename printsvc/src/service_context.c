/* SPDX-License-Identifier: MPL-2.0 */
#include "service_context.h"

#include "runtime_diagnostics.h"

#include <string.h>

void deneb_service_context_init(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    memset(svc, 0, sizeof(*svc));
    deneb_status_init(&svc->status);
    deneb_flow_init(&svc->flow);
    deneb_heater_wait_init(&svc->heater_wait);
    svc->serial.fd = -1;
}

int deneb_service_context_motion_runtime(deneb_print_service_t *svc,
                                         deneb_motion_runtime_t *runtime)
{
    if (!svc || !runtime)
        return -1;

    runtime->status = &svc->status;
    runtime->heater_wait = &svc->heater_wait;
    runtime->flow = &svc->flow;
    runtime->serial = &svc->serial;
    runtime->serial_ready = &svc->serial_ready;
    return 0;
}

int deneb_service_context_job_streamer(deneb_print_service_t *svc,
                                       deneb_job_streamer_t *streamer)
{
    if (!svc || !streamer)
        return -1;

    streamer->status = &svc->status;
    streamer->flow = &svc->flow;
    streamer->stream = &svc->job_stream;
    streamer->heater_wait = &svc->heater_wait;
    streamer->serial = &svc->serial;
    streamer->serial_ready = &svc->serial_ready;
    streamer->job_active = &svc->job_active;
    streamer->abort_requested = &svc->abort_requested;
    streamer->planner_starvation_count = &svc->planner_starvation_count;
    return 0;
}

void deneb_service_context_refresh_diagnostics(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    deneb_runtime_diagnostics_refresh(
        &svc->status, &svc->flow, svc->job_active,
        (unsigned int)svc->job_stream.line_number,
        svc->planner_starvation_count);
}

void deneb_service_context_close(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (!svc)
        return;

    deneb_gcode_stream_close(&svc->job_stream);
    svc->job_active = 0;

    if (deneb_service_context_motion_runtime(svc, &runtime) == 0)
        deneb_motion_runtime_close(&runtime);
}
