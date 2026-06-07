/* SPDX-License-Identifier: MPL-2.0 */
#include "status_parser.h"

#include <stdio.h>
#include <string.h>

static int has_prefix(const char *line, const char *prefix)
{
    return line && prefix && strncmp(line, prefix, strlen(prefix)) == 0;
}

static int parse_temperature_pair(const char *line, const char *key,
                                  float *current, float *target)
{
    const char *p = strstr(line, key);
    float cur = 0.0f;
    float set = 0.0f;
    int consumed = 0;

    if (!p)
        return 0;
    p += strlen(key);

    if (sscanf(p, "%f /%f%n", &cur, &set, &consumed) >= 1) {
        *current = cur;
        if (consumed > 0)
            *target = set;
        return 1;
    }

    return 0;
}

deneb_status_parse_result_t deneb_status_parse_marlin_line(deneb_status_t *status,
                                                           const char *line)
{
    float x, y, z, e;

    if (!status || !line || !*line)
        return DENEB_PARSE_NO_MATCH;

    if (strstr(line, "Error:") || strstr(line, "!!") || strstr(line, "Fault")) {
        status->fault = true;
        status->state = DENEB_PRINT_STATE_ERROR;
        snprintf(status->req, sizeof(status->req), "Error");
        return DENEB_PARSE_FAULT;
    }

    if (strstr(line, "FIRMWARE_NAME:") || strstr(line, "Cap:")) {
        return DENEB_PARSE_VERSION;
    }

    int parsed_temp = parse_temperature_pair(line, "T:", &status->head_t_cur,
                                             &status->head_t_set);
    parsed_temp |= parse_temperature_pair(line, "B:", &status->bed_t_cur,
                                          &status->bed_t_set);
    if (parsed_temp) {
        return DENEB_PARSE_TEMPERATURE;
    }

    if (has_prefix(line, "X:") &&
        sscanf(line, "X:%f Y:%f Z:%f E:%f", &x, &y, &z, &e) == 4) {
        status->x = x;
        status->y = y;
        status->z = z;
        status->e = e;
        return DENEB_PARSE_POSITION;
    }

    return DENEB_PARSE_NO_MATCH;
}
