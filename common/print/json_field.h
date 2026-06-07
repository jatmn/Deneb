/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_JSON_FIELD_H
#define DENEB_COMMON_JSON_FIELD_H

#include <stddef.h>

int deneb_json_get_value(const char *json, const char *key,
                         char *out, size_t out_sz);
float deneb_json_get_float(const char *json, const char *key, float def);
int deneb_json_get_int(const char *json, const char *key, int def);

#endif
