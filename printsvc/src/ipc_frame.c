/* SPDX-License-Identifier: MPL-2.0 */
#include "ipc_zmq.h"

#include "command.h"
#include "command_reply.h"

int deneb_printsvc_ipc_handle_frame(deneb_print_service_t *svc,
                                    const char *frame,
                                    char *reply, size_t reply_sz)
{
    deneb_command_t cmd;

    if (!svc || !frame || !reply || reply_sz == 0)
        return -1;

    if (deneb_command_parse(frame, &cmd) != 0) {
        deneb_command_reply_error(reply, reply_sz, "bad command");
        return -1;
    }

    return deneb_print_service_handle_command(svc, &cmd, reply, reply_sz);
}
