/* SPDX-License-Identifier: MPL-2.0 */
#include "gcode_control.h"

#include "command_reply.h"
#include "error_map.h"
#include "motion_sender.h"

int deneb_gcode_control_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    for (size_t i = 0; i < cmd->gcode_count; i++) {
        if (deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
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
}
