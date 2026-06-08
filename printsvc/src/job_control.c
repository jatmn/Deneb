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

    if (svc->job_active) {
        deneb_command_reply_error(reply, reply_sz, "job already active");
        return -1;
    }

    if (deneb_gcode_stream_open(&svc->job_stream, cmd->file) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_STORAGE,
                                             "failed to open job file");
        deneb_command_reply_error(reply, reply_sz, "failed to open job file");
        return -1;
    }

    svc->abort_requested = 0;
    svc->job_active = 1;
    deneb_job_lifecycle_start(&svc->status, cmd->file, cmd->source,
                              cmd->uuid, cmd->bed_target, cmd->head_target);
    deneb_heater_wait_start(&svc->heater_wait, svc->status.bed_t_set,
                            svc->status.head_t_set, 1.0f);

    deneb_command_reply_ok(reply, reply_sz, "job accepted");
    return 0;
}

int deneb_job_control_abort(deneb_print_service_t *svc,
                            char *reply, size_t reply_sz)
{
    deneb_motion_policy_t abort_policy;
    int policy_rc;

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
    svc->abort_requested = 1;
    policy_rc = deneb_motion_sender_apply_policy(&svc->flow, &svc->serial,
                                                 svc->serial_ready,
                                                 &abort_policy);
    if (policy_rc != 0) {
        svc->abort_requested = 0;
        svc->heater_wait.active = 0;
        deneb_job_lifecycle_error(&svc->status,
                                  deneb_error_make(
                                      deneb_motion_send_error_code(policy_rc),
                                      "abort cleanup failed"));
        deneb_command_reply_error(reply, reply_sz, "abort cleanup failed");
        return -1;
    }

    deneb_job_lifecycle_abort(&svc->status);
    svc->abort_requested = 0;
    svc->heater_wait.active = 0;
    deneb_command_reply_ok(reply, reply_sz, "abort accepted");
    return 0;
}
