/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_PAUSE_RESUME_CONTROL_H
#define DENEB_PRINTSVC_PAUSE_RESUME_CONTROL_H

#include "service.h"

int deneb_pause_resume_control_pause(deneb_print_service_t *svc,
                                     char *reply, size_t reply_sz);
int deneb_pause_resume_control_resume(deneb_print_service_t *svc,
                                      char *reply, size_t reply_sz);
int deneb_pause_resume_control_poll(deneb_print_service_t *svc);
int deneb_pause_resume_control_busy(const deneb_print_service_t *svc);

#endif
