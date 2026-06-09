/* SPDX-License-Identifier: MPL-2.0 */
#include "pause_resume_control.h"

#include "command_reply.h"
#include "error_map.h"
#include "job_lifecycle.h"
#include "motion_send_error.h"
#include "motion_sender.h"
#include "pause_resume.h"

static int position_is_valid(const deneb_status_t *status)
{
    return status && status->position_report_count > 0;
}

static void save_pause_state(deneb_print_service_t *svc)
{
    svc->paused_position_valid = position_is_valid(&svc->status);
    svc->paused_x = svc->status.x;
    svc->paused_y = svc->status.y;
    svc->paused_z = svc->status.z;
    svc->paused_e = svc->status.e;
    svc->paused_r0 = svc->status.r0;
    svc->paused_nozzle_setpoint = svc->status.head_t_set;
}

static void begin_pause_cleanup_from_current_position(deneb_print_service_t *svc)
{
    save_pause_state(svc);
    if (svc->paused_position_valid) {
        deneb_motion_policy_pause(&svc->pause_policy, svc->paused_x,
                                  svc->paused_y, svc->paused_z);
        svc->pause_policy_index = 0;
        svc->pause_policy_pending = 1;
    }
}

static int poll_pause_position_probe(deneb_print_service_t *svc)
{
    int send_rc;

    if (!svc || !svc->pause_position_probe_pending)
        return 0;

    if (!svc->pause_position_probe_sent) {
        if (deneb_flow_has_pending_barrier(&svc->flow) ||
            !deneb_flow_can_send(&svc->flow) ||
            deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return 0;
        send_rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                                 svc->serial_ready, "M114");
        if (send_rc != 0) {
            if (send_rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            svc->pause_position_probe_pending = 0;
            deneb_job_lifecycle_error(
                &svc->status,
                deneb_error_make(deneb_motion_send_error_code(send_rc),
                                 "pause position probe failed"));
            return -1;
        }
        svc->pause_position_probe_sent = 1;
        return 0;
    }

    if (deneb_flow_inflight(&svc->flow) != 0)
        return 0;
    if (svc->status.position_report_count <= svc->pause_position_report_start)
        return 0;

    svc->pause_position_probe_pending = 0;
    svc->pause_position_probe_sent = 0;
    svc->pause_position_report_start = 0;
    begin_pause_cleanup_from_current_position(svc);
    return svc->pause_policy_pending ? 0 : 1;
}

static int poll_policy(deneb_print_service_t *svc,
                       deneb_motion_policy_t *policy,
                       size_t *index,
                       int *pending,
                       const char *error_detail)
{
    if (!svc || !policy || !index || !pending || !*pending)
        return 0;

    while (*index < policy->count) {
        int send_rc;
        if (deneb_flow_has_pending_barrier(&svc->flow) ||
            !deneb_flow_can_send(&svc->flow) ||
            deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return 0;
        send_rc = deneb_motion_sender_send_gcode(
            &svc->flow, &svc->serial, svc->serial_ready,
            policy->commands[*index]);
        if (send_rc != 0) {
            if (send_rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            *pending = 0;
            *index = 0;
            deneb_job_lifecycle_error(
                &svc->status,
                deneb_error_make(deneb_motion_send_error_code(send_rc),
                                 error_detail));
            return -1;
        }
        (*index)++;
    }

    if (deneb_flow_inflight(&svc->flow) != 0)
        return 0;

    *pending = 0;
    *index = 0;
    return 1;
}

int deneb_pause_resume_control_pause(deneb_print_service_t *svc,
                                     char *reply, size_t reply_sz)
{
    if (!svc || !reply || reply_sz == 0)
        return -1;

    if (svc->resume_policy_pending) {
        deneb_command_reply_error(reply, reply_sz,
                                  "pause/resume already pending");
        return -1;
    }

    if (deneb_pause_resume_pause(&svc->status) < 0) {
        deneb_command_reply_error(reply, reply_sz, "no active print to pause");
        return -1;
    }

    svc->paused_position_valid = 0;
    if (svc->job_active && svc->status.state == DENEB_PRINT_STATE_PAUSED) {
        svc->pause_position_probe_pending = 1;
        svc->pause_position_probe_sent = 0;
        svc->pause_position_report_start = svc->status.position_report_count;
    } else {
        save_pause_state(svc);
    }

    deneb_command_reply_ok(reply, reply_sz, "pause accepted");
    return 0;
}

int deneb_pause_resume_control_resume(deneb_print_service_t *svc,
                                      char *reply, size_t reply_sz)
{
    if (!svc || !reply || reply_sz == 0)
        return -1;

    if (svc->resume_policy_pending) {
        deneb_command_reply_error(reply, reply_sz,
                                  "pause/resume already pending");
        return -1;
    }

    if (!svc->paused_position_valid) {
        if (deneb_pause_resume_resume(&svc->status,
                                      svc->heater_wait.active) < 0) {
            deneb_command_reply_error(reply, reply_sz, "print is not paused");
            return -1;
        }
        deneb_command_reply_ok(reply, reply_sz, "resume accepted");
        return 0;
    }

    if (svc->status.state != DENEB_PRINT_STATE_PAUSED) {
        deneb_command_reply_error(reply, reply_sz, "print is not paused");
        return -1;
    }

    deneb_motion_policy_resume(&svc->resume_policy, svc->paused_x,
                               svc->paused_y, svc->paused_z, svc->paused_e,
                               svc->paused_r0, svc->paused_nozzle_setpoint);
    svc->resume_policy_index = 0;
    svc->resume_policy_pending = 1;
    if (deneb_pause_resume_resume(&svc->status, 0) < 0) {
        svc->resume_policy_pending = 0;
        deneb_command_reply_error(reply, reply_sz, "print is not paused");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "resume accepted");
    return 0;
}

int deneb_pause_resume_control_poll(deneb_print_service_t *svc)
{
    int rc;
    int had_pause;

    if (!svc)
        return -1;

    if (svc->pause_position_probe_pending) {
        rc = poll_pause_position_probe(svc);
        if (rc <= 0)
            return rc;
    }

    had_pause = svc->pause_policy_pending;
    rc = poll_policy(svc, &svc->pause_policy, &svc->pause_policy_index,
                     &svc->pause_policy_pending, "pause cleanup failed");
    if (rc < 0 || (had_pause && rc == 0))
        return rc;

    rc = poll_policy(svc, &svc->resume_policy, &svc->resume_policy_index,
                     &svc->resume_policy_pending, "resume cleanup failed");
    if (rc < 0)
        return rc;
    if (rc > 0) {
        svc->paused_position_valid = 0;
    }
    return rc;
}

int deneb_pause_resume_control_busy(const deneb_print_service_t *svc)
{
    return svc && (svc->pause_position_probe_pending ||
                   svc->pause_policy_pending || svc->resume_policy_pending);
}
