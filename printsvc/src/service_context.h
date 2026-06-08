/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SERVICE_CONTEXT_H
#define DENEB_PRINTSVC_SERVICE_CONTEXT_H

#include "job_streamer.h"
#include "motion_runtime.h"
#include "service.h"

void deneb_service_context_init(deneb_print_service_t *svc);
int deneb_service_context_motion_runtime(deneb_print_service_t *svc,
                                         deneb_motion_runtime_t *runtime);
int deneb_service_context_job_streamer(deneb_print_service_t *svc,
                                       deneb_job_streamer_t *streamer);
void deneb_service_context_refresh_diagnostics(deneb_print_service_t *svc);
void deneb_service_context_close(deneb_print_service_t *svc);

#endif
