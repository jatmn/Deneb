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
    svc->job_stream.fd = -1;
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
    runtime->allow_sequence_resync =
        svc->job_active || svc->gcode_queue_active ||
        svc->abort_cleanup_pending || svc->finish_cleanup_pending;
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
    streamer->job_started_at = &svc->job_started_at;
    streamer->job_prepare_stage = &svc->job_prepare_stage;
    streamer->job_prepare_index = &svc->job_prepare_index;
    streamer->job_startup_index = &svc->job_startup_index;
    streamer->abort_requested = &svc->abort_requested;
    streamer->finish_cleanup_pending = &svc->finish_cleanup_pending;
    streamer->finish_cleanup_policy = &svc->finish_cleanup_policy;
    streamer->finish_cleanup_index = &svc->finish_cleanup_index;
    streamer->planner_starvation_count = &svc->planner_starvation_count;
    streamer->job_nozzle_resume_setpoint = &svc->job_nozzle_resume_setpoint;
    return 0;
}

void deneb_service_context_refresh_diagnostics(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    deneb_runtime_diagnostics_refresh(
        &svc->status, &svc->flow,
        svc->job_active || svc->finish_cleanup_pending ||
            svc->abort_cleanup_pending,
        (unsigned int)svc->job_stream.line_number,
        svc->planner_starvation_count);
}

void deneb_service_context_close(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (!svc)
        return;

    deneb_gcode_stream_close(&svc->job_stream);
    svc->job_stream.line_number = 0;
    svc->job_active = 0;
    svc->job_started_at = 0;
    svc->job_prepare_stage = 0;
    svc->job_prepare_index = 0;
    svc->job_startup_index = 0;
    svc->abort_requested = 0;
    svc->abort_cleanup_pending = 0;
    svc->abort_cleanup_index = 0;
    svc->finish_cleanup_pending = 0;
    svc->finish_cleanup_index = 0;
    svc->startup_status_probe_pending = 0;
    svc->firmware_probe_pending = 0;
    svc->firmware_probe_ticks = 0;
    svc->firmware_probe_attempts = 0;
    svc->pause_policy_pending = 0;
    svc->pause_policy_index = 0;
    svc->pause_position_probe_pending = 0;
    svc->pause_position_probe_sent = 0;
    svc->pause_position_report_start = 0;
    svc->resume_policy_pending = 0;
    svc->resume_policy_index = 0;
    svc->paused_position_valid = 0;
    svc->paused_nozzle_setpoint = 0.0f;
    svc->job_nozzle_resume_setpoint = 0.0f;
    svc->gcode_queue_count = 0;
    svc->gcode_queue_index = 0;
    svc->gcode_queue_active = 0;
    svc->finish_drain_ticks = 0;
    svc->finish_position_report_count = 0;
    svc->finish_stable_reports = 0;
    svc->heater_wait.active = 0;

    if (deneb_service_context_motion_runtime(svc, &runtime) == 0)
        deneb_motion_runtime_close(&runtime);
}
