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
