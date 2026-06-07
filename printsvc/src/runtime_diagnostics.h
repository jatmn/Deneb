/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_RUNTIME_DIAGNOSTICS_H
#define DENEB_PRINTSVC_RUNTIME_DIAGNOSTICS_H

#include "flow_control.h"
#include "status.h"

void deneb_runtime_diagnostics_refresh(deneb_status_t *status,
                                       const deneb_flow_control_t *flow,
                                       int job_active,
                                       unsigned int job_line_number,
                                       unsigned int planner_starvation_count);

#endif
