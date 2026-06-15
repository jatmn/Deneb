/* SPDX-License-Identifier: MPL-2.0 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void *lv_malloc(size_t size)
{
    return malloc(size);
}

void lv_free(void *data)
{
    free(data);
}

void *lv_realloc(void *data, size_t new_size)
{
    return realloc(data, new_size);
}

void lv_memcpy(void *dst, const void *src, size_t len)
{
    memcpy(dst, src, len);
}

void lv_memset(void *dst, uint8_t v, size_t len)
{
    memset(dst, v, len);
}
