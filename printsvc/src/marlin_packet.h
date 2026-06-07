/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MARLIN_PACKET_H
#define DENEB_PRINTSVC_MARLIN_PACKET_H

#include "config.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t sequence;
    char command[DENEB_PRINTSVC_MAX_GCODE_LINE];
} deneb_marlin_packet_t;

int deneb_marlin_packet_encode(uint8_t sequence, const char *command,
                               uint8_t *out, size_t out_sz, size_t *written);

#endif
