/* SPDX-License-Identifier: MPL-2.0 */
#include "flow_control.h"
#include "crc.h"
#include "marlin_packet.h"

#include <stdio.h>
#include <string.h>

static int command_starts_with(const char *command, const char *prefix)
{
    size_t len;

    if (!command || !prefix)
        return 0;
    len = strlen(prefix);
    return strncmp(command, prefix, len) == 0 &&
           (command[len] == '\0' || command[len] == ' ' ||
            command[len] == '\t');
}

static int command_is_barrier(const char *command)
{
    return command_starts_with(command, "G28") ||
           command_starts_with(command, "G90") ||
           command_starts_with(command, "G91") ||
           command_starts_with(command, "G92") ||
           command_starts_with(command, "M400") ||
           command_starts_with(command, "M82") ||
           command_starts_with(command, "M83") ||
           command_starts_with(command, "M109") ||
           command_starts_with(command, "M190");
}

void deneb_flow_init(deneb_flow_control_t *flow)
{
    memset(flow, 0, sizeof(*flow));
    flow->next_sequence = DENEB_FLOW_INITIAL_SEQUENCE;
}

void deneb_flow_clear_inflight(deneb_flow_control_t *flow)
{
    if (!flow)
        return;

    memset(flow->slots, 0, sizeof(flow->slots));
    flow->controller_free_space = 0;
    flow->controller_free_space_known = 0;
}

void deneb_flow_resync_to_expected(deneb_flow_control_t *flow)
{
    if (!flow)
        return;

    deneb_flow_clear_inflight(flow);
    flow->next_sequence = flow->last_proto_expected_sequence;
    flow->handling_resend = 0;
}

static deneb_flow_slot_t *slot_for_sequence(deneb_flow_control_t *flow, uint8_t sequence)
{
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].occupied && flow->slots[i].sequence == sequence)
            return &flow->slots[i];
    }
    return NULL;
}

static deneb_flow_slot_t *pending_slot_for_sequence(deneb_flow_control_t *flow,
                                                    uint8_t sequence)
{
    deneb_flow_slot_t *slot = slot_for_sequence(flow, sequence);
    if (!slot || slot->acknowledged)
        return NULL;
    return slot;
}

static deneb_flow_slot_t *first_free_slot(deneb_flow_control_t *flow)
{
    if (flow->controller_free_space_known && flow->controller_free_space == 0)
        return NULL;

    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (!flow->slots[i].occupied)
            return &flow->slots[i];
    }
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].acknowledged)
            return &flow->slots[i];
    }
    return NULL;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int parse_hex_byte(const char *text, uint8_t *out)
{
    int hi;
    int lo;

    if (!text || !out)
        return 0;
    hi = hex_value(text[0]);
    lo = hex_value(text[1]);
    if (hi < 0 || lo < 0)
        return 0;
    *out = (uint8_t)((hi << 4) | lo);
    return 1;
}

static int parse_sync_packet(const char *line, char *code, uint8_t *sequence,
                             uint8_t *free_space)
{
    uint8_t crc;
    uint8_t expected;

    if (!line || !code || !sequence || !free_space)
        return 0;
    if (strlen(line) != 7 || (line[0] != 'o' && line[0] != 'r' && line[0] != 'q'))
        return 0;
    if (!parse_hex_byte(line + 1, sequence) ||
        !parse_hex_byte(line + 3, free_space) ||
        !parse_hex_byte(line + 5, &crc))
        return 0;

    expected = deneb_crc8((const uint8_t *)line, 5);
    if (crc != expected)
        return -1;

    *code = line[0];
    return 1;
}

static int sequence_is_not_newer(uint8_t sequence, uint8_t acknowledged)
{
    return (uint8_t)(acknowledged - sequence) < DENEB_FLOW_WINDOW;
}

static void acknowledge_through(deneb_flow_control_t *flow, uint8_t sequence,
                                int include_sequence)
{
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        deneb_flow_slot_t *slot = &flow->slots[i];
        if (!slot->occupied || slot->acknowledged)
            continue;
        if (slot->sequence == sequence) {
            if (!include_sequence)
                continue;
        } else if (!sequence_is_not_newer(slot->sequence, sequence)) {
            continue;
        }
        slot->acknowledged = 1;
        flow->ack_count++;
    }
}

static int request_resend(deneb_flow_control_t *flow, deneb_flow_slot_t *slot,
                          uint8_t sequence, uint8_t *resend_sequence)
{
    flow->resend_count++;
    flow->reject_count++;
    flow->handling_resend = 1;
    slot->resend_attempts++;
    if (resend_sequence)
        *resend_sequence = sequence;
    return slot->resend_attempts > DENEB_FLOW_RESEND_ATTEMPT_LIMIT ? -1 : 2;
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

int deneb_flow_has_pending_barrier(const deneb_flow_control_t *flow)
{
    if (!flow)
        return 0;

    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        const deneb_flow_slot_t *slot = &flow->slots[i];
        if (slot->occupied && !slot->acknowledged && slot->barrier)
            return 1;
    }

    return 0;
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
    slot->barrier = command_is_barrier(command);
    slot->send_order = flow->sent_count;
    if (flow->controller_free_space_known && flow->controller_free_space > 0)
        flow->controller_free_space--;
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

static int parse_proto_expected_sequence(const char *line, uint8_t *sequence)
{
    unsigned int received;
    unsigned int expected;

    if (!line || !sequence)
        return 0;

    if (sscanf(line,
               "Error:ProtoError:Sequence number is unexpected (received, expected): %u,%u",
               &received, &expected) == 2 ||
        sscanf(line,
               "ProtoError:Sequence number is unexpected (received, expected): %u,%u",
               &received, &expected) == 2) {
        (void)received;
        *sequence = (uint8_t)expected;
        return 1;
    }

    return 0;
}

int deneb_flow_handle_response(deneb_flow_control_t *flow, const char *line,
                               uint8_t *resend_sequence)
{
    uint8_t seq = 0;
    uint8_t free_space = 0;
    char code = '\0';
    deneb_flow_slot_t *slot;
    int sync_rc;

    if (!flow || !line)
        return -1;
    if (resend_sequence)
        *resend_sequence = 0;

    sync_rc = parse_sync_packet(line, &code, &seq, &free_space);
    if (sync_rc < 0) {
        flow->reject_count++;
        return -1;
    }
    if (sync_rc > 0) {
        flow->controller_free_space = free_space;
        flow->controller_free_space_known = 1;
        if (code == 'q')
            return 0;
        if (code == 'o') {
            flow->handling_resend = 0;
            slot = slot_for_sequence(flow, seq);
            if (!slot)
                return 0;
            acknowledge_through(flow, seq, 1);
            return 1;
        }
        slot = pending_slot_for_sequence(flow, seq);
        if (!slot) {
            flow->reject_count++;
            return 0;
        }
        if (flow->handling_resend)
            return 0;
        acknowledge_through(flow, seq, 0);
        return request_resend(flow, slot, seq, resend_sequence);
    }

    if (parse_proto_expected_sequence(line, &seq)) {
        flow->last_proto_expected_sequence = seq;
        flow->reject_count++;
        return 3;
    }

    if (strstr(line, "Resend") || strstr(line, "rs ") || strstr(line, "rs N")) {
        if (flow->handling_resend)
            return 0;
        if (parse_response_sequence(line, &seq)) {
            if (!pending_slot_for_sequence(flow, seq)) {
                flow->reject_count++;
                return 0;
            }
            return request_resend(flow, pending_slot_for_sequence(flow, seq), seq,
                                  resend_sequence);
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
