/* SPDX-License-Identifier: MPL-2.0 */
#include "flow_control.h"
#include "marlin_packet.h"

#include <stdio.h>
#include <string.h>

void deneb_flow_init(deneb_flow_control_t *flow)
{
    memset(flow, 0, sizeof(*flow));
}

static deneb_flow_slot_t *slot_for_sequence(deneb_flow_control_t *flow, uint8_t sequence)
{
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].occupied && flow->slots[i].sequence == sequence)
            return &flow->slots[i];
    }
    return NULL;
}

static deneb_flow_slot_t *first_free_slot(deneb_flow_control_t *flow)
{
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (!flow->slots[i].occupied || flow->slots[i].acknowledged)
            return &flow->slots[i];
    }
    return NULL;
}

size_t deneb_flow_inflight(const deneb_flow_control_t *flow)
{
    size_t count = 0;

    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].occupied && !flow->slots[i].acknowledged)
            count++;
    }

    return count;
}

int deneb_flow_prepare_packet(deneb_flow_control_t *flow, const char *command,
                              uint8_t *out, size_t out_sz, size_t *written,
                              uint8_t *sequence)
{
    deneb_flow_slot_t *slot;
    uint8_t seq;

    if (!flow || !command || !out || !written || !sequence)
        return -1;

    slot = first_free_slot(flow);
    if (!slot)
        return -2;

    seq = flow->next_sequence++;
    if (deneb_marlin_packet_encode(seq, command, out, out_sz, written) != 0)
        return -1;

    memset(slot, 0, sizeof(*slot));
    slot->occupied = 1;
    slot->sequence = seq;
    snprintf(slot->command, sizeof(slot->command), "%s", command);
    flow->sent_count++;
    *sequence = seq;
    return 0;
}

static int parse_response_sequence(const char *line, uint8_t *sequence)
{
    unsigned int value;

    if (!line || !sequence)
        return 0;

    if (sscanf(line, "ok N%u", &value) == 1 ||
        sscanf(line, "ok %u", &value) == 1 ||
        sscanf(line, "Resend:%u", &value) == 1 ||
        sscanf(line, "Resend: %u", &value) == 1 ||
        sscanf(line, "rs N%u", &value) == 1 ||
        sscanf(line, "rs %u", &value) == 1) {
        *sequence = (uint8_t)value;
        return 1;
    }

    return 0;
}

int deneb_flow_handle_response(deneb_flow_control_t *flow, const char *line,
                               uint8_t *resend_sequence)
{
    uint8_t seq = 0;
    deneb_flow_slot_t *slot;

    if (!flow || !line)
        return -1;
    if (resend_sequence)
        *resend_sequence = 0;

    if (strstr(line, "Resend") || strstr(line, "rs ") || strstr(line, "rs N")) {
        if (parse_response_sequence(line, &seq)) {
            flow->resend_count++;
            if (resend_sequence)
                *resend_sequence = seq;
            return 2;
        }
        flow->reject_count++;
        return 1;
    }

    if (strncmp(line, "ok", 2) == 0) {
        if (parse_response_sequence(line, &seq)) {
            slot = slot_for_sequence(flow, seq);
            if (slot) {
                slot->acknowledged = 1;
                flow->ack_count++;
            }
        } else {
            for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
                if (flow->slots[i].occupied && !flow->slots[i].acknowledged) {
                    flow->slots[i].acknowledged = 1;
                    flow->ack_count++;
                    break;
                }
            }
        }
        return 1;
    }

    if (strstr(line, "Error")) {
        flow->reject_count++;
        return -1;
    }

    return 0;
}

int deneb_flow_get_resend_packet(deneb_flow_control_t *flow, uint8_t sequence,
                                 uint8_t *out, size_t out_sz, size_t *written)
{
    deneb_flow_slot_t *slot;

    if (!flow || !out || !written)
        return -1;

    slot = slot_for_sequence(flow, sequence);
    if (!slot)
        return -1;

    return deneb_marlin_packet_encode(slot->sequence, slot->command, out, out_sz,
                                      written);
}
