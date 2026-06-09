/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_JOB_CONTROL_H
#define DENEB_PRINTSVC_JOB_CONTROL_H

#include "command.h"
#include "service.h"

int deneb_job_control_accept(deneb_print_service_t *svc,
                             const deneb_command_t *cmd,
                             char *reply, size_t reply_sz);
int deneb_job_control_abort(deneb_print_service_t *svc,
                            char *reply, size_t reply_sz);
int deneb_job_control_poll_abort_cleanup(deneb_print_service_t *svc);
int deneb_job_control_poll_finish_cleanup(deneb_print_service_t *svc);

#endif
