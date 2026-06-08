/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SERVICE_COMMAND_H
#define DENEB_PRINTSVC_SERVICE_COMMAND_H

#include "command.h"
#include "service.h"

int deneb_service_command_handle(deneb_print_service_t *svc,
                                 const deneb_command_t *cmd,
                                 char *reply, size_t reply_sz);

#endif
