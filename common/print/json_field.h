/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_JSON_FIELD_H
#define DENEB_COMMON_JSON_FIELD_H

#include <stddef.h>

int deneb_json_get_value(const char *json, const char *key,
                         char *out, size_t out_sz);
/* Copy a value after its closing key quote, with the usual field decoding. */
int deneb_json_copy_value_span(const char *value, size_t len,
                               char *out, size_t out_sz);
int deneb_json_field_present(const char *json, const char *key);
int deneb_json_get_float_value(const char *json, const char *key, float *out);
int deneb_json_get_int_value(const char *json, const char *key, int *out);
int deneb_json_get_bool_value(const char *json, const char *key, int *out);
int deneb_json_value_is_truthy(const char *value);
int deneb_json_get_truthy_value(const char *json, const char *key, int *out);
float deneb_json_get_float(const char *json, const char *key, float def);
int deneb_json_get_int(const char *json, const char *key, int def);

#endif
