/* SPDX-License-Identifier: MPL-2.0 */
#include "status_parser.h"
#include "error_map.h"
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

static int parse_head_temperature(const char *line, float *current,
                                  float *target)
{
    if (parse_temperature_pair(line, "T:", current, target))
        return 1;

    for (unsigned int i = 0; i < 10; i++) {
        char key[4];
        snprintf(key, sizeof(key), "T%u:", i);
        if (parse_temperature_pair(line, key, current, target))
            return 1;
    }

    return 0;
}

static int parse_bed_temperature(const char *line, float *current,
                                 float *target)
{
    const char *p = line;
    float cur = 0.0f;
    float set = 0.0f;
    int consumed = 0;

    if (parse_temperature_pair(line, "B:", current, target))
        return 1;

    while ((p = strchr(p, 'B')) != NULL) {
        const char *v = p + 1;
        if (*v == '-' || *v == '+' || isdigit((unsigned char)*v)) {
            if (sscanf(v, "%f/%f%n", &cur, &set, &consumed) == 2 &&
                consumed > 0 && v[consumed] == '@') {
                *current = cur;
                *target = set;
                return 1;
            }
        }
        p++;
    }

    return 0;
}

static int parse_topcap_temperature(const char *line, float *current,
                                    bool *present)
{
    const char *p = line;
    int topcap_present = 0;
    float cur = 0.0f;
    int consumed = 0;

    while ((p = strchr(p, 't')) != NULL) {
        const char *v = p + 1;
        if (isdigit((unsigned char)*v) &&
            sscanf(v, "%d/%f%n", &topcap_present, &cur, &consumed) == 2 &&
            consumed > 0) {
            *present = topcap_present == 1;
            *current = cur;
            return 1;
        }
        p++;
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

static int parse_prefixed_value(const char *line, const char *key, float *out)
{
    const char *p;

    if (!line || !key || !out)
        return 0;
    p = strstr(line, key);
    if (!p)
        return 0;
    p += strlen(key);
    if (*p == ':' || *p == '=')
        p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    return sscanf(p, "%f", out) == 1;
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

static void copy_until_marker(const char *src, const char *marker,
                              char *out, size_t out_sz)
{
    size_t len;

    if (!src || !out || out_sz == 0)
        return;

    len = marker && strstr(src, marker) ?
        (size_t)(strstr(src, marker) - src) : strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1]))
        len--;
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, src, len);
    out[len] = '\0';
}

static int parse_version_line(const char *line, deneb_status_t *status)
{
    const char *firmware_name;
    const char *machine_type;
    char type[sizeof(status->machine_type)];
    char build[sizeof(status->firmware)];
    int pcb_id;

    machine_type = strstr(line, "MACHINE_TYPE:");
    if (machine_type &&
        sscanf(machine_type, "MACHINE_TYPE:%15s PCB_ID:%d BUILD:\"%63[^\"]\"",
               type, &pcb_id, build) == 3) {
        snprintf(status->machine_type, sizeof(status->machine_type), "%s",
                 type);
        status->pcb_id = pcb_id;
        status->pcb_id_valid = true;
        snprintf(status->firmware, sizeof(status->firmware), "%s", build);
        return 1;
    }

    firmware_name = strstr(line, "FIRMWARE_NAME:");
    if (firmware_name) {
        firmware_name += strlen("FIRMWARE_NAME:");
        copy_until_marker(firmware_name, " SOURCE_CODE_URL:", status->firmware,
                          sizeof(status->firmware));
        return 1;
    }

    if (strstr(line, "Cap:"))
        return 1;

    return 0;
}

deneb_status_parse_result_t deneb_status_parse_marlin_line(deneb_status_t *status,
                                                           const char *line)
{
    float x, y, z, e;

    if (!status || !line || !*line)
        return DENEB_PARSE_NO_MATCH;

    if ((strstr(line, "Error:") || strstr(line, "!!") || strstr(line, "Fault")) &&
        !deneb_error_line_is_recoverable_serial(line)) {
        status->fault = true;
        status->error = deneb_error_from_marlin_line(line);
        status->state = DENEB_PRINT_STATE_ERROR;
        snprintf(status->req, sizeof(status->req), "%s",
                 deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_ERROR));
        return DENEB_PARSE_FAULT;
    }

    if (parse_version_line(line, status)) {
        return DENEB_PARSE_VERSION;
    }

    int parsed_temp = parse_head_temperature(line, &status->head_t_cur,
                                             &status->head_t_set);
    parsed_temp |= parse_bed_temperature(line, &status->bed_t_cur,
                                         &status->bed_t_set);
    parsed_temp |= parse_topcap_temperature(line, &status->topcap_t_cur,
                                            &status->topcap_present);
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
        (void)parse_prefixed_value(line, "R0", &status->r0);
        status->position_report_count++;
        return DENEB_PARSE_POSITION;
    }

    return DENEB_PARSE_NO_MATCH;
}
