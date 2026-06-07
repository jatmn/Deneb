/* SPDX-License-Identifier: MPL-2.0 */
#include "status_parser.h"
#include "print_control.h"

#include <ctype.h>
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

static int contains_home_distance_marker(const char *line)
{
    char compact[64];
    size_t ci = 0;

    if (!line)
        return 0;

    for (size_t i = 0; line[i] && ci + 1 < sizeof(compact); i++) {
        if (isalnum((unsigned char)line[i]))
            compact[ci++] = (char)tolower((unsigned char)line[i]);
    }
    compact[ci] = '\0';

    return strstr(compact, "homedistance") != NULL ||
           strstr(compact, "g28distance") != NULL;
}

static int parse_axis_value(const char *line, char axis, float *out)
{
    const char *p = line;

    while ((p = strchr(p, axis)) != NULL) {
        const char *v = p + 1;
        while (*v && isspace((unsigned char)*v))
            v++;
        if (*v == ':' || *v == '=')
            v++;
        else {
            p++;
            continue;
        }
        while (*v && isspace((unsigned char)*v))
            v++;
        if (sscanf(v, "%f", out) == 1)
            return 1;
        p++;
    }

    return 0;
}

static int parse_home_distance(const char *line, deneb_status_t *status)
{
    float x = status->home_x;
    float y = status->home_y;
    float z = status->home_z;
    int matched = 0;

    if (!contains_home_distance_marker(line))
        return 0;

    matched |= parse_axis_value(line, 'X', &x);
    matched |= parse_axis_value(line, 'Y', &y);
    matched |= parse_axis_value(line, 'Z', &z);
    matched |= parse_axis_value(line, 'x', &x);
    matched |= parse_axis_value(line, 'y', &y);
    matched |= parse_axis_value(line, 'z', &z);
    if (!matched)
        return 0;

    status->home_x = x;
    status->home_y = y;
    status->home_z = z;
    status->home_distance_valid = true;
    return 1;
}

deneb_status_parse_result_t deneb_status_parse_marlin_line(deneb_status_t *status,
                                                           const char *line)
{
    float x, y, z, e;

    if (!status || !line || !*line)
        return DENEB_PARSE_NO_MATCH;

    if (strstr(line, "Error:") || strstr(line, "!!") || strstr(line, "Fault")) {
        status->fault = true;
        status->error = deneb_error_from_marlin_line(line);
        status->state = DENEB_PRINT_STATE_ERROR;
        snprintf(status->req, sizeof(status->req), "%s",
                 deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_ERROR));
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

    if (parse_home_distance(line, status))
        return DENEB_PARSE_HOME_DISTANCE;

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
