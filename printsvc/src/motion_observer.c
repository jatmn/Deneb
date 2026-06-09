/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_observer.h"
#include "status_parser.h"

#include <stdio.h>

int deneb_motion_observer_handle_line(deneb_status_t *status,
                                      deneb_heater_wait_t *heater_wait,
                                      deneb_flow_control_t *flow,
                                      const char *line,
                                      uint8_t *resend_sequence)
{
    int flow_rc;

    if (!status || !heater_wait || !flow || !line)
        return -1;

    deneb_status_parse_marlin_line(status, line);
    if (deneb_heater_wait_ready(heater_wait, status))
        heater_wait->active = 0;
    else
        deneb_heater_wait_apply_status(heater_wait, status);

    flow_rc = deneb_flow_handle_response(flow, line, resend_sequence);
    if (flow_rc != 0) {
        int n = snprintf(status->flow_last_response,
                         sizeof(status->flow_last_response), "%s", line);
        if (n < 0)
            status->flow_last_response[0] = '\0';
    }
    return flow_rc;
}
