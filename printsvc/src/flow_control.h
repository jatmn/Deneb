/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_FLOW_CONTROL_H
#define DENEB_PRINTSVC_FLOW_CONTROL_H

#include "config.h"

#include <stddef.h>
#include <stdint.h>

#define DENEB_FLOW_WINDOW 8

typedef struct {
    uint8_t sequence;
    char command[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int occupied;
    int acknowledged;
} deneb_flow_slot_t;

typedef struct {
    uint8_t next_sequence;
    deneb_flow_slot_t slots[DENEB_FLOW_WINDOW];
    unsigned int sent_count;
    unsigned int ack_count;
    unsigned int resend_count;
    unsigned int reject_count;
} deneb_flow_control_t;

void deneb_flow_init(deneb_flow_control_t *flow);
int deneb_flow_prepare_packet(deneb_flow_control_t *flow, const char *command,
                              uint8_t *out, size_t out_sz, size_t *written,
                              uint8_t *sequence);
int deneb_flow_handle_response(deneb_flow_control_t *flow, const char *line,
                               uint8_t *resend_sequence);
int deneb_flow_get_resend_packet(deneb_flow_control_t *flow, uint8_t sequence,
                                 uint8_t *out, size_t out_sz, size_t *written);
size_t deneb_flow_inflight(const deneb_flow_control_t *flow);

#endif
