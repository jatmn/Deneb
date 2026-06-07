/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_DIAGNOSTICS_LOG_H
#define DENEB_PRINTSVC_DIAGNOSTICS_LOG_H

#include "command.h"
#include "status.h"

int deneb_diagnostics_log_open(const char *path);
void deneb_diagnostics_log_close(void);
void deneb_diagnostics_log_status(const deneb_status_t *status, int force);
void deneb_diagnostics_log_command(const deneb_command_t *cmd, int rc,
                                   unsigned int latency_ms);

#endif
