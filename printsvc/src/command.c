/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "command.h"
#include "command_format.h"
#include "json_field.h"

#include <string.h>

static deneb_command_type_t command_type_from_verb(const char *verb)
{
    if (strcmp(verb, DENEB_COMMAND_VERB_GCODE) == 0) return DENEB_COMMAND_GCODE;
    if (strcmp(verb, DENEB_COMMAND_VERB_MACRO) == 0) return DENEB_COMMAND_MACRO;
    if (strcmp(verb, DENEB_COMMAND_VERB_JOB) == 0) return DENEB_COMMAND_JOB;
    if (strcmp(verb, DENEB_COMMAND_VERB_ABORT) == 0) return DENEB_COMMAND_ABORT;
    if (strcmp(verb, DENEB_COMMAND_VERB_PAUSE) == 0) return DENEB_COMMAND_PAUSE;
    if (strcmp(verb, DENEB_COMMAND_VERB_RESUME) == 0) return DENEB_COMMAND_RESUME;
    return DENEB_COMMAND_UNKNOWN;
}

const char *deneb_command_type_name(deneb_command_type_t type)
{
    switch (type) {
        case DENEB_COMMAND_GCODE: return DENEB_COMMAND_VERB_GCODE;
        case DENEB_COMMAND_MACRO: return DENEB_COMMAND_VERB_MACRO;
        case DENEB_COMMAND_JOB: return DENEB_COMMAND_VERB_JOB;
        case DENEB_COMMAND_ABORT: return DENEB_COMMAND_VERB_ABORT;
        case DENEB_COMMAND_PAUSE: return DENEB_COMMAND_VERB_PAUSE;
        case DENEB_COMMAND_RESUME: return DENEB_COMMAND_VERB_RESUME;
        default: return "UNKNOWN";
    }
}

int deneb_command_extract_json_string(const char *json, const char *key,
                                      char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz == 0)
        return -1;
    out[0] = '\0';
    return deneb_json_get_value(json, key, out, out_sz);
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
        deneb_command_extract_json_string(out->payload, "cloud_job_id",
                                         out->cloud_job_id,
                                         sizeof(out->cloud_job_id));
        deneb_json_get_float_value(out->payload, "bedTset", &out->bed_target);
        if (out->bed_target <= 0.0f)
            deneb_json_get_float_value(out->payload, "bed_temperature", &out->bed_target);
        deneb_json_get_float_value(out->payload, "headTset", &out->head_target);
        if (out->head_target <= 0.0f)
            deneb_json_get_float_value(out->payload, "nozzle_temperature", &out->head_target);
    } else if (out->type == DENEB_COMMAND_MACRO) {
        deneb_command_extract_json_string(out->payload, "macro", out->macro,
                                         sizeof(out->macro));
    } else if (out->type == DENEB_COMMAND_GCODE) {
        return parse_gcode_array(out->payload, out);
    }

    return 0;
}
