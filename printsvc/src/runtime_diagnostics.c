/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime_diagnostics.h"

void deneb_runtime_diagnostics_refresh(deneb_status_t *status,
                                       const deneb_flow_control_t *flow,
                                       int job_active,
                                       unsigned int job_line_number,
                                       unsigned int planner_starvation_count)
{
    if (!status || !flow)
        return;

    status->flow_inflight = (unsigned int)deneb_flow_inflight(flow);
    status->flow_sent = flow->sent_count;
    status->flow_ack = flow->ack_count;
    status->flow_resend = flow->resend_count;
    status->flow_reject = flow->reject_count;
    status->job_queue_depth = job_active ? 1u : 0u;
    status->job_line_number = job_active ? job_line_number : 0u;
    status->planner_starvation_count = planner_starvation_count;
}
