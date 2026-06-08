/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_sender.h"

int deneb_motion_sender_send_gcode(deneb_flow_control_t *flow,
                                   deneb_serial_transport_t *serial,
                                   int serial_ready,
                                   const char *line)
{
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;
    uint8_t sequence = 0;

    if (!flow || !line || !*line)
        return DENEB_MOTION_SEND_INVALID;

    int rc = deneb_flow_prepare_packet(flow, line, packet, sizeof(packet),
                                       &written, &sequence);
    if (rc != 0)
        return rc == -2 ? DENEB_MOTION_SEND_FLOW_FULL :
                          DENEB_MOTION_SEND_INVALID;

    if (!serial_ready)
        return 0;
    if (!serial)
        return DENEB_MOTION_SEND_SERIAL;

    return deneb_serial_write_all(serial, packet, written) == 0 ?
               0 :
               DENEB_MOTION_SEND_SERIAL;
}

int deneb_motion_sender_resend_sequence(deneb_flow_control_t *flow,
                                        deneb_serial_transport_t *serial,
                                        int serial_ready,
                                        uint8_t sequence)
{
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;

    if (!flow)
        return DENEB_MOTION_SEND_INVALID;

    if (deneb_flow_get_resend_packet(flow, sequence, packet,
                                     sizeof(packet), &written) != 0)
        return DENEB_MOTION_SEND_FLOW_FULL;

    if (!serial_ready)
        return 0;
    if (!serial)
        return DENEB_MOTION_SEND_SERIAL;

    return deneb_serial_write_all(serial, packet, written) == 0 ?
               0 :
               DENEB_MOTION_SEND_SERIAL;
}

int deneb_motion_sender_apply_policy(deneb_flow_control_t *flow,
                                     deneb_serial_transport_t *serial,
                                     int serial_ready,
                                     const deneb_motion_policy_t *policy)
{
    if (!policy)
        return -1;

    for (size_t i = 0; i < policy->count; i++) {
        if (deneb_motion_sender_send_gcode(flow, serial, serial_ready,
                                           policy->commands[i]) != 0)
            return -1;
    }
    return 0;
}
