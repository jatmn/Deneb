/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_CRC_H
#define DENEB_PRINTSVC_CRC_H

#include <stddef.h>
#include <stdint.h>

uint8_t deneb_crc8(const uint8_t *data, size_t len);
uint16_t deneb_crc16_ccitt(const uint8_t *data, size_t len);

#endif
