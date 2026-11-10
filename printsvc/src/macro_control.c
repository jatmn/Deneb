/* SPDX-License-Identifier: MPL-2.0 */
#include "macro_control.h"
#include "command_reply.h"
#include "gcode_control.h"
#include "macro_registry.h"

int deneb_macro_control_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz)
{
    char path[256];
    if (!svc || !cmd || !reply || !reply_sz)
        return -1;
    if (deneb_macro_resolve(cmd->macro, path, sizeof(path)) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "macro failed");
        deneb_command_reply_error(reply, reply_sz, "macro failed");
        return -1;
    }
    /* Admission only. The main poll loop owns execution and remains available
     * to receive ABORT while motion or a heater wait is outstanding. */
    return deneb_gcode_control_run_file(svc, path, reply, reply_sz);
}
