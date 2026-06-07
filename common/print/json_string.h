/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_JSON_STRING_H
#define DENEB_COMMON_JSON_STRING_H

#include <stddef.h>

void deneb_json_escape_string(const char *src, char *out, size_t out_sz);

#endif
