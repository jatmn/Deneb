/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SHA256_H
#define DENEB_PRINTSVC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_len;
} deneb_sha256_t;

void deneb_sha256_init(deneb_sha256_t *ctx);
void deneb_sha256_update(deneb_sha256_t *ctx, const uint8_t *data, size_t len);
void deneb_sha256_final(deneb_sha256_t *ctx, uint8_t digest[32]);
void deneb_sha256_hex(const uint8_t digest[32], char out[65]);

#endif
