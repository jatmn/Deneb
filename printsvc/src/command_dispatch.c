/* SPDX-License-Identifier: MPL-2.0 */
#include "command_dispatch.h"

#include "command_reply.h"
#include "error_map.h"
#include "job_control.h"
#include "macro_control.h"
#include "motion_sender.h"
#include "pause_resume.h"

int deneb_command_dispatch_handle(deneb_print_service_t *svc,
                                  const deneb_command_t *cmd,
                                  char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    switch (cmd->type) {
        case DENEB_COMMAND_GCODE:
            for (size_t i = 0; i < cmd->gcode_count; i++) {
                if (deneb_motion_sender_send_gcode(&svc->flow,
                                                   &svc->serial,
                                                   svc->serial_ready,
                                                   cmd->gcode[i]) != 0) {
                    svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND,
                                                         "gcode failed");
                    deneb_command_reply_error(reply, reply_sz, "gcode failed");
                    return -1;
                }
            }
            deneb_command_reply_ok(reply, reply_sz, "gcode accepted");
            return 0;

        case DENEB_COMMAND_MACRO:
            return deneb_macro_control_run(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_JOB:
            return deneb_job_control_accept(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_ABORT:
            return deneb_job_control_abort(svc, reply, reply_sz);

        case DENEB_COMMAND_PAUSE:
            if (deneb_pause_resume_pause(&svc->status) < 0) {
                deneb_command_reply_error(reply, reply_sz,
                                          "no active print to pause");
                return -1;
            }
            deneb_command_reply_ok(reply, reply_sz, "pause accepted");
            return 0;

        case DENEB_COMMAND_RESUME:
            if (deneb_pause_resume_resume(&svc->status,
                                          svc->heater_wait.active) < 0) {
                deneb_command_reply_error(reply, reply_sz,
                                          "print is not paused");
                return -1;
            }
            deneb_command_reply_ok(reply, reply_sz, "resume accepted");
            return 0;

        default:
            deneb_command_reply_error(reply, reply_sz, "unknown command");
            return -1;
    }
}
