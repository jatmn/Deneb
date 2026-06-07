/* SPDX-License-Identifier: MPL-2.0 */
#include "json_field.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int deneb_json_get_value(const char *json, const char *key,
                         char *out, size_t out_sz)
{
    char search[128];
    const char *p;
    size_t i = 0;

    if (!json || !key || !out || out_sz == 0)
        return -1;
    out[0] = '\0';

    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p)
        return -1;

    p += strlen(search);
    while (*p && (is_space((unsigned char)*p) || *p == ':'))
        p++;

    if (*p == '"') {
        p++;
        while (*p && *p != '"' && i < out_sz - 1) {
            if (*p == '\\' && p[1])
                p++;
            out[i++] = *p++;
        }
        if (*p != '"')
            return -1;
    } else {
        while (*p && *p != ',' && *p != '}' &&
               !is_space((unsigned char)*p) && i < out_sz - 1)
            out[i++] = *p++;
    }

    out[i] = '\0';
    return 0;
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
