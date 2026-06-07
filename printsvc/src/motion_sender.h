/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_SENDER_H
#define DENEB_PRINTSVC_MOTION_SENDER_H

#include "flow_control.h"
#include "motion_policy.h"
#include "serial_transport.h"

int deneb_motion_sender_send_gcode(deneb_flow_control_t *flow,
                                   deneb_serial_transport_t *serial,
                                   int serial_ready,
                                   const char *line);
int deneb_motion_sender_resend_sequence(deneb_flow_control_t *flow,
                                        deneb_serial_transport_t *serial,
                                        int serial_ready,
                                        uint8_t sequence);
int deneb_motion_sender_apply_policy(deneb_flow_control_t *flow,
                                     deneb_serial_transport_t *serial,
                                     int serial_ready,
                                     const deneb_motion_policy_t *policy);

#endif
