/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Minimal MD5 for HTTP Digest auth (RFC 2617).
 */

#ifndef MD5_H
#define MD5_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buf[64];
} md5_ctx_t;

/* Standard MD5: init, update, final */
void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const void *data, size_t len);
void md5_final(md5_ctx_t *ctx, uint8_t digest[16]);

/* Convenience: MD5 hex string of data */
void md5_hex(const void *data, size_t len, char out[33]);

/* Convenience: MD5 hex of "a:b" concatenation */
void md5_hex_concat(const char *a, const char *b, char out[33]);

#endif /* MD5_H */
