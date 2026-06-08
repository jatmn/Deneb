/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_IPC_ZMQ_H
#define DENEB_PRINTSVC_IPC_ZMQ_H

#include "service.h"

#include <stddef.h>

int deneb_printsvc_ipc_handle_frame(deneb_print_service_t *svc,
                                    const char *frame,
                                    char *reply, size_t reply_sz);
int deneb_printsvc_ipc_run(deneb_print_service_t *svc);

#endif
