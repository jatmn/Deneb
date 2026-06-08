/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_COMMAND_AUDIT_H
#define DENEB_PRINTSVC_COMMAND_AUDIT_H

#include "command.h"
#include "service.h"

typedef int (*deneb_command_audit_handler_t)(void *ctx,
                                             const deneb_command_t *cmd,
                                             char *reply, size_t reply_sz);

unsigned int deneb_command_audit_elapsed_ms(long long start_ms,
                                            long long end_ms);
int deneb_command_audit_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz,
                            deneb_command_audit_handler_t handler,
                            void *handler_ctx);

#endif
