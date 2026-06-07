/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_OBSERVER_H
#define DENEB_PRINTSVC_MOTION_OBSERVER_H

#include "flow_control.h"
#include "heater_wait.h"
#include "status.h"

int deneb_motion_observer_handle_line(deneb_status_t *status,
                                      deneb_heater_wait_t *heater_wait,
                                      deneb_flow_control_t *flow,
                                      const char *line,
                                      uint8_t *resend_sequence);

#endif
