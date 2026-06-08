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
        int rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                                svc->serial_ready,
                                                cmd->gcode[i]);
        if (rc != 0) {
            deneb_error_code_t code =
                rc == DENEB_MOTION_SEND_SERIAL ? DENEB_ERROR_SERIAL :
                                                  DENEB_ERROR_COMMAND;
            svc->status.error = deneb_error_make(code, "gcode failed");
            deneb_command_reply_error(reply, reply_sz, "gcode failed");
            return -1;
        }
    }

    deneb_command_reply_ok(reply, reply_sz, "gcode accepted");
    return 0;
}
