/* SPDX-License-Identifier: MPL-2.0 */
#include "job_control.h"

#include "command_reply.h"
#include "error_map.h"
#include "job_lifecycle.h"
#include "motion_policy.h"
#include "motion_send_error.h"
#include "motion_sender.h"

static int job_control_abortable(const deneb_print_service_t *svc)
{
    if (!svc)
        return 0;
    return svc->job_active ||
           svc->gcode_queue_active ||
           svc->status.bed_t_set > 0.0f ||
           svc->status.head_t_set > 0.0f ||
           svc->status.state == DENEB_PRINT_STATE_PREPARING ||
           svc->status.state == DENEB_PRINT_STATE_PRINTING ||
           svc->status.state == DENEB_PRINT_STATE_PAUSED ||
           svc->status.state == DENEB_PRINT_STATE_ABORTING;
}

int deneb_job_control_accept(deneb_print_service_t *svc,
                             const deneb_command_t *cmd,
                             char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    if (!cmd->file[0]) {
        deneb_command_reply_error(reply, reply_sz, "missing job file");
        return -1;
    }

    if (svc->job_active || svc->finish_cleanup_pending ||
        svc->abort_cleanup_pending || svc->gcode_queue_active ||
        svc->pause_policy_pending || svc->pause_position_probe_pending ||
        svc->resume_policy_pending) {
        deneb_command_reply_error(reply, reply_sz, "job already active");
        return -1;
    }

    if (deneb_gcode_stream_open(&svc->job_stream, cmd->file) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_STORAGE,
                                             "failed to open job file");
        deneb_command_reply_error(reply, reply_sz, "failed to open job file");
        return -1;
    }

    deneb_flow_clear_inflight(&svc->flow);
    svc->abort_requested = 0;
    svc->abort_cleanup_pending = 0;
    svc->pause_policy_pending = 0;
    svc->pause_policy_index = 0;
    svc->pause_position_probe_pending = 0;
    svc->pause_position_probe_sent = 0;
    svc->pause_position_report_start = 0;
    svc->resume_policy_pending = 0;
    svc->resume_policy_index = 0;
    svc->paused_position_valid = 0;
    svc->finish_cleanup_pending = 0;
    svc->finish_cleanup_index = 0;
    svc->finish_drain_ticks = 0;
    svc->finish_position_report_count = 0;
    svc->finish_stable_reports = 0;
    svc->job_active = 1;
    deneb_job_lifecycle_start(&svc->status, cmd->file, cmd->source,
                              cmd->uuid, cmd->cloud_job_id,
                              cmd->bed_target, cmd->head_target);
    deneb_heater_wait_start(&svc->heater_wait, svc->status.bed_t_set,
                            svc->status.head_t_set, 1.0f);

    deneb_command_reply_ok(reply, reply_sz, "job accepted");
    return 0;
}

int deneb_job_control_abort(deneb_print_service_t *svc,
                            char *reply, size_t reply_sz)
{
    deneb_motion_policy_t abort_policy;

    if (!svc || !reply || reply_sz == 0)
        return -1;

    if (!job_control_abortable(svc)) {
        svc->abort_requested = 0;
        deneb_command_reply_error(reply, reply_sz, "no active print to abort");
        return -1;
    }

    deneb_motion_policy_abort(&abort_policy);
    if (svc->job_active) {
        deneb_gcode_stream_close(&svc->job_stream);
        svc->job_active = 0;
    }
    svc->abort_requested = 0;
    svc->finish_cleanup_pending = 0;
    svc->finish_cleanup_index = 0;
    svc->pause_policy_pending = 0;
    svc->pause_policy_index = 0;
    svc->pause_position_probe_pending = 0;
    svc->pause_position_probe_sent = 0;
    svc->pause_position_report_start = 0;
    svc->resume_policy_pending = 0;
    svc->resume_policy_index = 0;
    svc->paused_position_valid = 0;
    svc->gcode_queue_count = 0;
    svc->gcode_queue_index = 0;
    svc->gcode_queue_active = 0;
    svc->finish_drain_ticks = 0;
    svc->finish_position_report_count = 0;
    svc->finish_stable_reports = 0;
    svc->abort_cleanup_policy = abort_policy;
    svc->abort_cleanup_index = 0;
    svc->abort_cleanup_pending = 1;
    svc->heater_wait.active = 0;
    svc->status.bed_t_set = 0.0f;
    svc->status.head_t_set = 0.0f;
    deneb_job_lifecycle_aborting(&svc->status);
    if (!svc->serial_ready) {
        deneb_flow_clear_inflight(&svc->flow);
        svc->abort_cleanup_pending = 0;
        svc->abort_cleanup_index = 0;
        svc->job_stream.line_number = 0;
        deneb_job_lifecycle_abort(&svc->status);
    }
    deneb_command_reply_ok(reply, reply_sz, "abort accepted");
    return 0;
}

int deneb_job_control_poll_abort_cleanup(deneb_print_service_t *svc)
{
    if (!svc || !svc->abort_cleanup_pending)
        return 0;

    while (svc->abort_cleanup_index < svc->abort_cleanup_policy.count) {
        int send_rc;
        if (deneb_flow_has_pending_barrier(&svc->flow) ||
            !deneb_flow_can_send(&svc->flow) ||
            deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return 0;
        send_rc = deneb_motion_sender_send_gcode(
            &svc->flow, &svc->serial, svc->serial_ready,
            svc->abort_cleanup_policy.commands[svc->abort_cleanup_index]);
        if (send_rc != 0) {
            if (send_rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            svc->abort_requested = 0;
            svc->abort_cleanup_pending = 0;
            svc->abort_cleanup_index = 0;
            svc->heater_wait.active = 0;
            deneb_job_lifecycle_error(&svc->status,
                                      deneb_error_make(
                                          deneb_motion_send_error_code(send_rc),
                                          "abort cleanup failed"));
            return -1;
        }
        svc->abort_cleanup_index++;
    }

    if (deneb_flow_inflight(&svc->flow) != 0)
        return 0;

    svc->abort_cleanup_pending = 0;
    svc->abort_cleanup_index = 0;
    svc->abort_requested = 0;
    svc->heater_wait.active = 0;
    deneb_flow_clear_inflight(&svc->flow);
    svc->job_stream.line_number = 0;
    deneb_job_lifecycle_abort(&svc->status);
    return 1;
}

int deneb_job_control_poll_finish_cleanup(deneb_print_service_t *svc)
{
    if (!svc || !svc->finish_cleanup_pending)
        return 0;

    while (svc->finish_cleanup_index < svc->finish_cleanup_policy.count) {
        int send_rc;
        if (deneb_flow_has_pending_barrier(&svc->flow) ||
            !deneb_flow_can_send(&svc->flow) ||
            deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return 0;
        send_rc = deneb_motion_sender_send_gcode(
            &svc->flow, &svc->serial, svc->serial_ready,
            svc->finish_cleanup_policy.commands[svc->finish_cleanup_index]);
        if (send_rc != 0) {
            if (send_rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            svc->finish_cleanup_pending = 0;
            svc->finish_cleanup_index = 0;
            svc->finish_drain_ticks = 0;
            svc->heater_wait.active = 0;
            deneb_job_lifecycle_error(
                &svc->status,
                deneb_error_make(deneb_motion_send_error_code(send_rc),
                                 "finish cleanup failed"));
            return -1;
        }
        svc->finish_cleanup_index++;
    }

    if (deneb_flow_inflight(&svc->flow) != 0)
        return 0;

    if (!svc->serial_ready) {
        svc->finish_cleanup_pending = 0;
        svc->finish_cleanup_index = 0;
        svc->finish_drain_ticks = 0;
        svc->finish_stable_reports = 0;
        svc->status.finish_drain_ticks = 0;
        svc->status.finish_stable_reports = 0;
        svc->abort_requested = 0;
        svc->heater_wait.active = 0;
        deneb_flow_clear_inflight(&svc->flow);
        svc->job_stream.line_number = 0;
        deneb_job_lifecycle_complete(&svc->status);
        svc->job_active = 0;
        return 1;
    }

    svc->finish_cleanup_pending = 0;
    svc->finish_cleanup_index = 0;
    svc->finish_drain_ticks = 0;
    svc->finish_stable_reports = 0;
    svc->status.finish_drain_ticks = 0;
    svc->status.finish_stable_reports = 0;
    svc->abort_requested = 0;
    svc->heater_wait.active = 0;
    deneb_flow_clear_inflight(&svc->flow);
    svc->job_stream.line_number = 0;
    deneb_job_lifecycle_complete(&svc->status);
    svc->job_active = 0;
    return 1;
}
