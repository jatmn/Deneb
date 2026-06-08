/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "command_audit.h"
#include "command_dispatch.h"
#include "job_streamer.h"
#include "motion_runtime.h"
#include "runtime_diagnostics.h"
#include "service.h"

#include <stdio.h>
#include <string.h>

void deneb_print_service_init(deneb_print_service_t *svc)
{
    memset(svc, 0, sizeof(*svc));
    deneb_status_init(&svc->status);
    deneb_flow_init(&svc->flow);
    deneb_heater_wait_init(&svc->heater_wait);
    svc->serial.fd = -1;
}

int deneb_print_service_open_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (!svc)
        return -1;

    runtime.status = &svc->status;
    runtime.heater_wait = &svc->heater_wait;
    runtime.flow = &svc->flow;
    runtime.serial = &svc->serial;
    runtime.serial_ready = &svc->serial_ready;

    return deneb_motion_runtime_open(&runtime);
}

int deneb_print_service_poll_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (!svc)
        return -1;

    runtime.status = &svc->status;
    runtime.heater_wait = &svc->heater_wait;
    runtime.flow = &svc->flow;
    runtime.serial = &svc->serial;
    runtime.serial_ready = &svc->serial_ready;

    return deneb_motion_runtime_poll(&runtime);
}

void deneb_print_service_refresh_diagnostics(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    deneb_runtime_diagnostics_refresh(
        &svc->status, &svc->flow, svc->job_active,
        (unsigned int)svc->job_stream.line_number,
        svc->planner_starvation_count);
}

static int service_dispatch_command(void *ctx, const deneb_command_t *cmd,
                                    char *reply, size_t reply_sz)
{
    return deneb_command_dispatch_handle((deneb_print_service_t *)ctx, cmd,
                                         reply, reply_sz);
}

int deneb_print_service_handle_command(deneb_print_service_t *svc,
                                       const deneb_command_t *cmd,
                                       char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    return deneb_command_audit_run(svc, cmd, reply, reply_sz,
                                   service_dispatch_command, svc);
}

int deneb_print_service_poll_job(deneb_print_service_t *svc)
{
    deneb_job_streamer_t streamer;

    if (!svc)
        return -1;

    streamer.status = &svc->status;
    streamer.flow = &svc->flow;
    streamer.stream = &svc->job_stream;
    streamer.heater_wait = &svc->heater_wait;
    streamer.serial = &svc->serial;
    streamer.serial_ready = svc->serial_ready;
    streamer.job_active = &svc->job_active;
    streamer.abort_requested = &svc->abort_requested;
    streamer.planner_starvation_count = &svc->planner_starvation_count;

    return deneb_job_streamer_poll(&streamer);
}

void deneb_print_service_close(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (!svc)
        return;
    deneb_gcode_stream_close(&svc->job_stream);
    svc->job_active = 0;

    runtime.status = &svc->status;
    runtime.heater_wait = &svc->heater_wait;
    runtime.flow = &svc->flow;
    runtime.serial = &svc->serial;
    runtime.serial_ready = &svc->serial_ready;
    deneb_motion_runtime_close(&runtime);
}
