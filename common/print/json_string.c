/* SPDX-License-Identifier: MPL-2.0 */
#include "json_string.h"

void deneb_json_escape_string(const char *src, char *out, size_t out_sz)
{
    size_t oi = 0;

    if (!out || out_sz == 0)
        return;

    for (size_t i = 0; src && src[i] && oi + 1 < out_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c == '"' || c == '\\') && oi + 2 < out_sz) {
            out[oi++] = '\\';
            out[oi++] = (char)c;
        } else if (c >= 0x20) {
            out[oi++] = (char)c;
        }
    }
    out[oi] = '\0';
}
