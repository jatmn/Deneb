/* SPDX-License-Identifier: MPL-2.0 */
#include "json_field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Keep the C-string path free of span checks in the shared decoder. */
static inline __attribute__((always_inline))
int copy_value(const char *p, size_t len, char *out, size_t out_sz, int bounded)
{
    size_t i = 0;

    out[0] = '\0';
    while ((!bounded || len) && (is_space((unsigned char)*p) || *p == ':')) {
        p++;
        if (bounded)
            len--;
    }

    if ((!bounded || len) && *p == '"') {
        p++;
        if (bounded)
            len--;
        while ((!bounded || len) && *p && *p != '"' && i < out_sz - 1) {
            if (*p == '\\' && (!bounded || len > 1) && p[1]) {
                p++;
                if (bounded)
                    len--;
            }
            out[i++] = *p++;
            if (bounded)
                len--;
        }
        if ((bounded && !len) || *p != '"')
            return -1;
    } else {
        while ((!bounded || len) && *p && *p != ',' && *p != '}' &&
               !is_space((unsigned char)*p) && i < out_sz - 1) {
            out[i++] = *p++;
            if (bounded)
                len--;
        }
    }

    out[i] = '\0';
    return 0;
}

int deneb_json_copy_value_span(const char *value, size_t len,
                               char *out, size_t out_sz)
{
    if (!value || !out || out_sz == 0)
        return -1;
    return copy_value(value, len, out, out_sz, 1);
}

int deneb_json_get_value(const char *json, const char *key,
                         char *out, size_t out_sz)
{
    char search[128];

    if (!json || !key || !out || out_sz == 0)
        return -1;
    out[0] = '\0';
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    return p ? copy_value(p + strlen(search), 0, out, out_sz, 0) : -1;
}

int deneb_json_field_present(const char *json, const char *key)
{
    char search[128];

    if (!json || !key)
        return 0;
    snprintf(search, sizeof(search), "\"%s\"", key);
    return strstr(json, search) != NULL;
}

int deneb_json_get_float_value(const char *json, const char *key, float *out)
{
    char tmp[64];
    char *end = NULL;
    float parsed;

    if (!out || deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return -1;

    parsed = strtof(tmp, &end);
    if (end == tmp)
        return -1;
    while (*end && is_space((unsigned char)*end))
        end++;
    if (*end)
        return -1;

    *out = parsed;
    return 0;
}

int deneb_json_get_int_value(const char *json, const char *key, int *out)
{
    char tmp[64];
    char *end = NULL;
    long parsed;

    if (!out || deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return -1;

    parsed = strtol(tmp, &end, 10);
    if (end == tmp)
        return -1;
    while (*end && is_space((unsigned char)*end))
        end++;
    if (*end)
        return -1;

    *out = (int)parsed;
    return 0;
}

int deneb_json_get_bool_value(const char *json, const char *key, int *out)
{
    char tmp[16];

    if (!out || deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return -1;
    if (strcmp(tmp, "true") == 0) {
        *out = 1;
        return 0;
    }
    if (strcmp(tmp, "false") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

int deneb_json_value_is_truthy(const char *value)
{
    if (!value)
        return 0;
    return strcmp(value, "true") == 0 ||
           strcmp(value, "t") == 0 ||
           strcmp(value, "yes") == 0 ||
           strcmp(value, "1") == 0;
}

int deneb_json_get_truthy_value(const char *json, const char *key, int *out)
{
    char tmp[32];

    if (!out || deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return -1;
    *out = deneb_json_value_is_truthy(tmp);
    return 0;
}

float deneb_json_get_float(const char *json, const char *key, float def)
{
    char tmp[64];

    if (deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return def;
    return strtof(tmp, NULL);
}

int deneb_json_get_int(const char *json, const char *key, int def)
{
    char tmp[64];

    if (deneb_json_get_value(json, key, tmp, sizeof(tmp)) != 0)
        return def;
    return (int)strtol(tmp, NULL, 10);
}
