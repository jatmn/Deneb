/* SPDX-License-Identifier: MPL-2.0 */
#include "service_command.h"

#include "command_audit.h"
#include "command_dispatch.h"

static int service_command_dispatch(void *ctx, const deneb_command_t *cmd,
                                    char *reply, size_t reply_sz)
{
    return deneb_command_dispatch_handle((deneb_print_service_t *)ctx, cmd,
                                         reply, reply_sz);
}

int deneb_service_command_handle(deneb_print_service_t *svc,
                                 const deneb_command_t *cmd,
                                 char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    return deneb_command_audit_run(svc, cmd, reply, reply_sz,
                                   service_command_dispatch, svc);
}
