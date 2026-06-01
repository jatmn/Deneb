/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Minimal MD5 implementation for HTTP Digest auth (RFC 2617).
 * Based on RFC 1321. No external dependencies.
 */

#include "md5.h"
#include <string.h>

/* F, G, H, I are basic MD5 functions */
#define F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~(z))))

/* Rotate left */
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* Round functions */
#define FF(a, b, c, d, x, s, ac) { (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTL((a), (s)); (a) += (b); }
#define GG(a, b, c, d, x, s, ac) { (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTL((a), (s)); (a) += (b); }
#define HH(a, b, c, d, x, s, ac) { (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTL((a), (s)); (a) += (b); }
#define II(a, b, c, d, x, s, ac) { (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = ROTL((a), (s)); (a) += (b); }

static const uint32_t T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d7, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];

    /* Decode block into 16 uint32_t (little-endian) */
    for (int i = 0; i < 16; i++) {
        x[i] = (uint32_t)block[i*4]
             | ((uint32_t)block[i*4+1] << 8)
             | ((uint32_t)block[i*4+2] << 16)
             | ((uint32_t)block[i*4+3] << 24);
    }

    /* Round 1 */
    FF(a, b, c, d, x[ 0],  7, T[ 0]); FF(d, a, b, c, x[ 1], 12, T[ 1]);
    FF(c, d, a, b, x[ 2], 17, T[ 2]); FF(b, c, d, a, x[ 3], 22, T[ 3]);
    FF(a, b, c, d, x[ 4],  7, T[ 4]); FF(d, a, b, c, x[ 5], 12, T[ 5]);
    FF(c, d, a, b, x[ 6], 17, T[ 6]); FF(b, c, d, a, x[ 7], 22, T[ 7]);
    FF(a, b, c, d, x[ 8],  7, T[ 8]); FF(d, a, b, c, x[ 9], 12, T[ 9]);
    FF(c, d, a, b, x[10], 17, T[10]); FF(b, c, d, a, x[11], 22, T[11]);
    FF(a, b, c, d, x[12],  7, T[12]); FF(d, a, b, c, x[13], 12, T[13]);
    FF(c, d, a, b, x[14], 17, T[14]); FF(b, c, d, a, x[15], 22, T[15]);

    /* Round 2 */
    GG(a, b, c, d, x[ 1],  5, T[16]); GG(d, a, b, c, x[ 6],  9, T[17]);
    GG(c, d, a, b, x[11], 14, T[18]); GG(b, c, d, a, x[ 0], 20, T[19]);
    GG(a, b, c, d, x[ 5],  5, T[20]); GG(d, a, b, c, x[10],  9, T[21]);
    GG(c, d, a, b, x[15], 14, T[22]); GG(b, c, d, a, x[ 4], 20, T[23]);
    GG(a, b, c, d, x[ 9],  5, T[24]); GG(d, a, b, c, x[14],  9, T[25]);
    GG(c, d, a, b, x[ 3], 14, T[26]); GG(b, c, d, a, x[ 8], 20, T[27]);
    GG(a, b, c, d, x[13],  5, T[28]); GG(d, a, b, c, x[ 2],  9, T[29]);
    GG(c, d, a, b, x[ 7], 14, T[30]); GG(b, c, d, a, x[12], 20, T[31]);

    /* Round 3 */
    HH(a, b, c, d, x[ 5],  4, T[32]); HH(d, a, b, c, x[ 8], 11, T[33]);
    HH(c, d, a, b, x[11], 16, T[34]); HH(b, c, d, a, x[14], 23, T[35]);
    HH(a, b, c, d, x[ 1],  4, T[36]); HH(d, a, b, c, x[ 4], 11, T[37]);
    HH(c, d, a, b, x[ 7], 16, T[38]); HH(b, c, d, a, x[10], 23, T[39]);
    HH(a, b, c, d, x[13],  4, T[40]); HH(d, a, b, c, x[ 0], 11, T[41]);
    HH(c, d, a, b, x[ 3], 16, T[42]); HH(b, c, d, a, x[ 6], 23, T[43]);
    HH(a, b, c, d, x[ 9],  4, T[44]); HH(d, a, b, c, x[12], 11, T[45]);
    HH(c, d, a, b, x[15], 16, T[46]); HH(b, c, d, a, x[ 2], 23, T[47]);

    /* Round 4 */
    II(a, b, c, d, x[ 0],  6, T[48]); II(d, a, b, c, x[ 7], 10, T[49]);
    II(c, d, a, b, x[14], 15, T[50]); II(b, c, d, a, x[ 5], 21, T[51]);
    II(a, b, c, d, x[12],  6, T[52]); II(d, a, b, c, x[ 3], 10, T[53]);
    II(c, d, a, b, x[10], 15, T[54]); II(b, c, d, a, x[ 1], 21, T[55]);
    II(a, b, c, d, x[ 8],  6, T[56]); II(d, a, b, c, x[15], 10, T[57]);
    II(c, d, a, b, x[ 6], 15, T[58]); II(b, c, d, a, x[13], 21, T[59]);
    II(a, b, c, d, x[ 4],  6, T[60]); II(d, a, b, c, x[11], 10, T[61]);
    II(c, d, a, b, x[ 2], 15, T[62]); II(b, c, d, a, x[ 9], 21, T[63]);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void md5_init(md5_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = (size_t)(ctx->count & 0x3f);

    ctx->count += len;

    /* Fill buffer if partial */
    if (idx) {
        size_t part = 64 - idx;
        if (len >= part) {
            memcpy(ctx->buf + idx, p, part);
            md5_transform(ctx->state, ctx->buf);
            p += part; len -= part;
            idx = 0;
        } else {
            memcpy(ctx->buf + idx, p, len);
            return;
        }
    }

    /* Process full blocks */
    while (len >= 64) {
        md5_transform(ctx->state, p);
        p += 64; len -= 64;
    }

    /* Buffer remainder */
    if (len) memcpy(ctx->buf, p, len);
}

void md5_final(md5_ctx_t *ctx, uint8_t digest[16])
{
    uint64_t bits = ctx->count * 8;
    size_t idx = (size_t)(ctx->count & 0x3f);

    /* Pad with 0x80 */
    ctx->buf[idx++] = 0x80;

    /* If not enough room for length, pad and transform */
    if (idx > 56) {
        memset(ctx->buf + idx, 0, 64 - idx);
        md5_transform(ctx->state, ctx->buf);
        idx = 0;
    }
    memset(ctx->buf + idx, 0, 56 - idx);

    /* Append length in bits (little-endian) */
    for (int i = 0; i < 8; i++)
        ctx->buf[56 + i] = (uint8_t)(bits >> (i * 8));

    md5_transform(ctx->state, ctx->buf);

    /* Output digest (little-endian) */
    for (int i = 0; i < 4; i++) {
        digest[i*4+0] = (uint8_t)(ctx->state[i]);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

void md5_hex(const void *data, size_t len, char out[33])
{
    md5_ctx_t ctx;
    uint8_t digest[16];
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
    for (int i = 0; i < 16; i++) {
        out[i*2]   = "0123456789abcdef"[digest[i] >> 4];
        out[i*2+1] = "0123456789abcdef"[digest[i] & 0x0f];
    }
    out[32] = '\0';
}

void md5_hex_concat(const char *a, const char *b, char out[33])
{
    md5_ctx_t ctx;
    uint8_t digest[16];
    md5_init(&ctx);
    md5_update(&ctx, a, strlen(a));
    md5_update(&ctx, ":", 1);
    md5_update(&ctx, b, strlen(b));
    md5_final(&ctx, digest);
    for (int i = 0; i < 16; i++) {
        out[i*2]   = "0123456789abcdef"[digest[i] >> 4];
        out[i*2+1] = "0123456789abcdef"[digest[i] & 0x0f];
    }
    out[32] = '\0';
}
