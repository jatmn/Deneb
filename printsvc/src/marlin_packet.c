/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "crc.h"
#include "marlin_packet.h"

#include <stdio.h>
#include <string.h>

int deneb_marlin_packet_encode(uint8_t sequence, const char *command,
                               uint8_t *out, size_t out_sz, size_t *written)
{
    size_t command_len;
    uint16_t crc;
    size_t total_len;

    if (!command || !out || !written)
        return -1;

    command_len = strlen(command);
    if (command_len > DENEB_PRINTSVC_MAX_GCODE_LINE || command_len > 255)
        return -1;
    total_len = 2 + 1 + 1 + command_len + 2;
    if (total_len > out_sz)
        return -1;

    out[0] = 0xff;
    out[1] = 0xff;
    out[2] = sequence;
    out[3] = (uint8_t)command_len;
    memcpy(out + 4, command, command_len);

    crc = deneb_crc16_ccitt(out + 2, command_len + 2);
    out[4 + command_len] = (uint8_t)(crc >> 8);
    out[5 + command_len] = (uint8_t)(crc & 0xff);

    *written = total_len;
    return 0;
}
