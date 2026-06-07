/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static deneb_command_type_t command_type_from_verb(const char *verb)
{
    if (strcmp(verb, "GCODE") == 0) return DENEB_COMMAND_GCODE;
    if (strcmp(verb, "MACRO") == 0) return DENEB_COMMAND_MACRO;
    if (strcmp(verb, "JOB") == 0) return DENEB_COMMAND_JOB;
    if (strcmp(verb, "ABORT") == 0) return DENEB_COMMAND_ABORT;
    if (strcmp(verb, "PAUSE") == 0) return DENEB_COMMAND_PAUSE;
    if (strcmp(verb, "RESUME") == 0) return DENEB_COMMAND_RESUME;
    return DENEB_COMMAND_UNKNOWN;
}

const char *deneb_command_type_name(deneb_command_type_t type)
{
    switch (type) {
        case DENEB_COMMAND_GCODE: return "GCODE";
        case DENEB_COMMAND_MACRO: return "MACRO";
        case DENEB_COMMAND_JOB: return "JOB";
        case DENEB_COMMAND_ABORT: return "ABORT";
        case DENEB_COMMAND_PAUSE: return "PAUSE";
        case DENEB_COMMAND_RESUME: return "RESUME";
        default: return "UNKNOWN";
    }
}

int deneb_command_extract_json_string(const char *json, const char *key,
                                      char *out, size_t out_sz)
{
    char needle[64];
    const char *p;
    const char *end;
    size_t len;

    if (!json || !key || !out || out_sz == 0)
        return -1;
    out[0] = '\0';

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return -1;
    p++;

    end = p;
    while (*end) {
        if (*end == '"' && (end == p || end[-1] != '\\'))
            break;
        end++;
    }
    if (*end != '"') return -1;

    len = (size_t)(end - p);
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return len > 0 ? 0 : -1;
}

static int extract_json_float(const char *json, const char *key, float *out)
{
    char needle[64];
    const char *p;

    if (!json || !key || !out)
        return -1;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (sscanf(p, "%f", out) == 1)
        return 0;
    return -1;
}

static int parse_gcode_array(const char *payload, deneb_command_t *out)
{
    const char *p = payload;

    while ((p = strchr(p, '"')) != NULL &&
           out->gcode_count < DENEB_PRINTSVC_MAX_COMMANDS) {
        const char *start = ++p;
        const char *end = start;
        size_t len;

        while (*end) {
            if (*end == '"' && (end == start || end[-1] != '\\'))
                break;
            end++;
        }
        if (*end != '"')
            return -1;

        len = (size_t)(end - start);
        if (len >= DENEB_PRINTSVC_MAX_GCODE_LINE)
            len = DENEB_PRINTSVC_MAX_GCODE_LINE - 1;
        memcpy(out->gcode[out->gcode_count], start, len);
        out->gcode[out->gcode_count][len] = '\0';
        out->gcode_count++;
        p = end + 1;
    }

    return out->gcode_count > 0 ? 0 : -1;
}

int deneb_command_parse(const char *frame, deneb_command_t *out)
{
    const char *sep;
    size_t verb_len;
    size_t payload_len;

    if (!frame || !out)
        return -1;
    memset(out, 0, sizeof(*out));

    sep = strchr(frame, '<');
    if (!sep)
        return -1;

    verb_len = (size_t)(sep - frame);
    if (verb_len == 0 || verb_len >= sizeof(out->verb))
        return -1;
    memcpy(out->verb, frame, verb_len);
    out->verb[verb_len] = '\0';

    out->type = command_type_from_verb(out->verb);
    if (out->type == DENEB_COMMAND_UNKNOWN)
        return -1;

    payload_len = strlen(sep + 1);
    if (payload_len >= sizeof(out->payload))
        payload_len = sizeof(out->payload) - 1;
    memcpy(out->payload, sep + 1, payload_len);
    out->payload[payload_len] = '\0';

    if (out->type == DENEB_COMMAND_JOB) {
        if (deneb_command_extract_json_string(out->payload, "file", out->file,
                                             sizeof(out->file)) != 0) {
            deneb_command_extract_json_string(out->payload, "path", out->file,
                                             sizeof(out->file));
        }
        deneb_command_extract_json_string(out->payload, "source", out->source,
                                         sizeof(out->source));
        deneb_command_extract_json_string(out->payload, "uuid", out->uuid,
                                         sizeof(out->uuid));
        extract_json_float(out->payload, "bedTset", &out->bed_target);
        if (out->bed_target <= 0.0f)
            extract_json_float(out->payload, "bed_temperature", &out->bed_target);
        extract_json_float(out->payload, "headTset", &out->head_target);
        if (out->head_target <= 0.0f)
            extract_json_float(out->payload, "nozzle_temperature", &out->head_target);
    } else if (out->type == DENEB_COMMAND_MACRO) {
        deneb_command_extract_json_string(out->payload, "macro", out->macro,
                                         sizeof(out->macro));
    } else if (out->type == DENEB_COMMAND_GCODE) {
        return parse_gcode_array(out->payload, out);
    }

    return 0;
}
