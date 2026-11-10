/* SPDX-License-Identifier: MPL-2.0 */
#include "service_command.h"
#include "service_context.h"
#include "service.h"
#include "gcode_control.h"
#include "job_control.h"
#include "motion_sender.h"
#include "pause_resume_control.h"

#define DENEB_PRINTSVC_TEMP_POLL_TICKS 4
#define DENEB_PRINTSVC_FIRMWARE_PROBE_INTERVAL_TICKS 20
#define DENEB_PRINTSVC_FIRMWARE_PROBE_MAX_ATTEMPTS 6

int deneb_print_service_stop_faulted_work(deneb_print_service_t *svc)
{
    if (!svc || (!svc->status.fault && svc->status.state != DENEB_PRINT_STATE_ERROR))
        return 0;
    if (!svc->fault_shutdown_started) {
        svc->fault_shutdown_started = 1;
        /* Retire pre-fault movement so it cannot be retransmitted. Preserve
         * sequence numbering. Shutdown is best effort, not proof of cooling. */
        deneb_flow_clear_inflight(&svc->flow);
        if (svc->serial_ready) {
            static const char *shutdown[] = {"M401", "M104 S0", "M140 S0"};
            for (size_t i = 0; i < sizeof(shutdown) / sizeof(shutdown[0]); ++i) {
                if (deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                                   svc->serial_ready, shutdown[i]) != 0)
                    break;
            }
        }
    }
    deneb_gcode_stream_close(&svc->job_stream);
    svc->job_active = 0;
    svc->job_prepare_stage = 0;
    svc->gcode_queue_active = 0;
    svc->gcode_queue_count = 0;
    svc->gcode_queue_index = 0;
    svc->pause_position_probe_pending = 0;
    svc->pause_policy_pending = 0;
    svc->resume_policy_pending = 0;
    svc->abort_cleanup_pending = 0;
    svc->finish_cleanup_pending = 0;
    svc->heater_wait.active = 0;
    svc->paused_position_valid = 0;
    svc->abort_requested = 0;
    if (svc->status.manual_state == DENEB_MANUAL_RUNNING ||
        svc->status.manual_state == DENEB_MANUAL_CANCELLING)
        svc->status.manual_state = DENEB_MANUAL_ERROR;
    /* Preserve the controller fault and observed targets. Never park/home or
     * claim cooling/recovery merely because host submission has stopped. */
    svc->status.state = DENEB_PRINT_STATE_ERROR;
    return 1;
}

static int status_has_firmware(const deneb_print_service_t *svc)
{
    return svc && svc->status.firmware[0] != '\0';
}

static int send_status_probe(deneb_print_service_t *svc, int include_version)
{
    int rc;

    if (include_version) {
        if (!deneb_flow_can_send(&svc->flow))
            return 0;
        rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                            svc->serial_ready, "M115");
        if (rc == DENEB_MOTION_SEND_FLOW_FULL)
            return 0;
        if (rc != 0)
            return rc;
    }

    if (!deneb_flow_can_send(&svc->flow))
        return 0;
    rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                        svc->serial_ready, "M105");
    if (rc == DENEB_MOTION_SEND_FLOW_FULL)
        return 0;
    if (rc != 0)
        return rc;

    if (!deneb_flow_can_send(&svc->flow))
        return 0;
    rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                        svc->serial_ready, "M114");
    return rc == DENEB_MOTION_SEND_FLOW_FULL ? 0 : rc;
}

void deneb_print_service_init(deneb_print_service_t *svc)
{
    deneb_service_context_init(svc);
}

int deneb_print_service_open_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (deneb_service_context_motion_runtime(svc, &runtime) < 0)
        return -1;

    if (deneb_motion_runtime_open(&runtime) != 0)
        return -1;

    svc->startup_status_probe_pending = 1;
    svc->firmware_probe_pending = 1;
    svc->firmware_probe_ticks = 0;
    svc->firmware_probe_attempts = 0;
    svc->temperature_poll_ticks = 0;
    return 0;
}

int deneb_print_service_poll_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;
    int rc;

    if (deneb_service_context_motion_runtime(svc, &runtime) < 0)
        return -1;

    deneb_print_service_stop_faulted_work(svc);
    rc = deneb_motion_runtime_poll(&runtime);

    if (deneb_print_service_stop_faulted_work(svc))
        return -1;
    deneb_pause_resume_control_poll(svc);
    deneb_job_control_poll_abort_cleanup(svc);
    deneb_job_control_poll_finish_cleanup(svc);
    deneb_gcode_control_refresh_manual_status(svc);
    if (status_has_firmware(svc))
        svc->firmware_probe_pending = 0;
    if (svc->serial_ready &&
        svc->startup_status_probe_pending &&
        deneb_flow_inflight(&svc->flow) == 0) {
        int probe_rc = send_status_probe(svc, 1);
        if (probe_rc == 0) {
            svc->startup_status_probe_pending = 0;
            svc->temperature_poll_ticks = 0;
            svc->firmware_probe_ticks = 0;
            svc->firmware_probe_attempts++;
        }
        if (probe_rc != 0 && rc == 0)
            rc = probe_rc;
    }
    if (svc->serial_ready &&
        svc->firmware_probe_pending &&
        !svc->startup_status_probe_pending &&
        !svc->job_active &&
        !svc->abort_cleanup_pending &&
        !svc->finish_cleanup_pending &&
        deneb_flow_inflight(&svc->flow) == 0) {
        if (status_has_firmware(svc) ||
            svc->firmware_probe_attempts >=
                DENEB_PRINTSVC_FIRMWARE_PROBE_MAX_ATTEMPTS) {
            svc->firmware_probe_pending = 0;
        } else if (++svc->firmware_probe_ticks >=
                   DENEB_PRINTSVC_FIRMWARE_PROBE_INTERVAL_TICKS) {
            int version_rc = deneb_motion_sender_send_gcode(
                &svc->flow, &svc->serial, svc->serial_ready, "M115");
            if (version_rc == 0) {
                svc->firmware_probe_ticks = 0;
                svc->firmware_probe_attempts++;
            }
            if (version_rc != 0 && rc == 0)
                rc = version_rc;
        }
    }
    if (svc->serial_ready &&
        !svc->startup_status_probe_pending &&
        deneb_flow_inflight(&svc->flow) == 0 &&
        ++svc->temperature_poll_ticks >= DENEB_PRINTSVC_TEMP_POLL_TICKS) {
        int poll_rc = send_status_probe(svc, 0);
        svc->temperature_poll_ticks = 0;
        if (poll_rc != 0 && rc == 0)
            rc = poll_rc;
    }
    return rc;
}

void deneb_print_service_refresh_diagnostics(deneb_print_service_t *svc)
{
    deneb_service_context_refresh_diagnostics(svc);
}

int deneb_print_service_handle_command(deneb_print_service_t *svc,
                                       const deneb_command_t *cmd,
                                       char *reply, size_t reply_sz)
{
    return deneb_service_command_handle(svc, cmd, reply, reply_sz);
}

int deneb_print_service_poll_job(deneb_print_service_t *svc)
{
    deneb_job_streamer_t streamer;
    char abort_reply[128];

    if (deneb_service_context_job_streamer(svc, &streamer) < 0)
        return -1;
    if (deneb_print_service_stop_faulted_work(svc))
        return -1;
    if (svc->abort_requested)
        return deneb_job_control_abort(svc, abort_reply, sizeof(abort_reply));
    if (deneb_pause_resume_control_busy(svc))
        return 0;
    if (svc->gcode_queue_active)
        return deneb_gcode_control_poll(svc);

    return deneb_job_streamer_poll(&streamer);
}

void deneb_print_service_close(deneb_print_service_t *svc)
{
    deneb_service_context_close(svc);
}
