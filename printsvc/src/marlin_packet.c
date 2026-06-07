/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "crc.h"
#include "marlin_packet.h"

#include <stdio.h>
#include <string.h>

int deneb_marlin_packet_encode(uint8_t sequence, const char *command,
                               uint8_t *out, size_t out_sz, size_t *written)
{
    char body[DENEB_PRINTSVC_MAX_GCODE_LINE + 32];
    uint16_t crc;
    int body_len;
    int total_len;

    if (!command || !out || !written)
        return -1;

    body_len = snprintf(body, sizeof(body), "N%u %s", (unsigned int)sequence, command);
    if (body_len < 0 || (size_t)body_len >= sizeof(body))
        return -1;

    crc = deneb_crc16_ccitt((const uint8_t *)body, (size_t)body_len);
    total_len = snprintf((char *)out, out_sz, "%s*%u\n", body, (unsigned int)crc);
    if (total_len < 0 || (size_t)total_len >= out_sz)
        return -1;

    *written = (size_t)total_len;
    return 0;
}
