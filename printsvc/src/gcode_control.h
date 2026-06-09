/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_GCODE_CONTROL_H
#define DENEB_PRINTSVC_GCODE_CONTROL_H

#include "command.h"
#include "service.h"

int deneb_gcode_control_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz);
int deneb_gcode_control_poll(deneb_print_service_t *svc);

#endif
