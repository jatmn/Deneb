/* SPDX-License-Identifier: MPL-2.0 */
#include "command_dispatch.h"

#include "command_reply.h"
#include "gcode_control.h"
#include "job_control.h"
#include "macro_control.h"
#include "pause_resume_control.h"

int deneb_command_dispatch_handle(deneb_print_service_t *svc,
                                  const deneb_command_t *cmd,
                                  char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    switch (cmd->type) {
        case DENEB_COMMAND_GCODE:
            return deneb_gcode_control_run(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_MACRO:
            return deneb_macro_control_run(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_JOB:
            return deneb_job_control_accept(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_ABORT:
            return deneb_job_control_abort(svc, reply, reply_sz);

        case DENEB_COMMAND_PAUSE:
            return deneb_pause_resume_control_pause(svc, reply, reply_sz);

        case DENEB_COMMAND_RESUME:
            return deneb_pause_resume_control_resume(svc, reply, reply_sz);

        default:
            deneb_command_reply_error(reply, reply_sz, "unknown command");
            return -1;
    }
}
