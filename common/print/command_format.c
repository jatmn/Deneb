/* SPDX-License-Identifier: MPL-2.0 */
#include "command_format.h"
#include "json_field.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>

static int append(char *out, size_t out_sz, size_t *pos, const char *text)
{
    size_t len;

    if (!out || !pos || !text || *pos >= out_sz)
        return -1;
    len = strlen(text);
    if (len >= out_sz - *pos)
        return -1;
    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';
    return 0;
}

static int append_escaped(char *out, size_t out_sz, size_t *pos,
                          const char *text)
{
    if (!text)
        text = "";

    for (size_t i = 0; text[i]; i++) {
        char tmp[3] = {0};
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            if (append(out, out_sz, pos, tmp) != 0)
                return -1;
        } else if (c >= 0x20) {
            tmp[0] = (char)c;
            if (append(out, out_sz, pos, tmp) != 0)
                return -1;
        }
    }

    return 0;
}

int deneb_command_format_gcode(const char *const *lines, size_t count,
                               char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!lines || count == 0 || !out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (append(out, out_sz, &pos, DENEB_COMMAND_VERB_GCODE "<[") != 0)
        return -1;
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && append(out, out_sz, &pos, ",") != 0)
            return -1;
        if (append(out, out_sz, &pos, "\"") != 0 ||
            append_escaped(out, out_sz, &pos, lines[i]) != 0 ||
            append(out, out_sz, &pos, "\"") != 0)
            return -1;
    }
    if (append(out, out_sz, &pos, "]") != 0)
        return -1;
    return (int)pos;
}

int deneb_command_format_macro(const char *macro, char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!macro || !*macro || !out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (append(out, out_sz, &pos, DENEB_COMMAND_VERB_MACRO "<{\"macro\":\"") != 0 ||
        append_escaped(out, out_sz, &pos, macro) != 0 ||
        append(out, out_sz, &pos, "\"}") != 0)
        return -1;
    return (int)pos;
}

int deneb_command_format_job(const char *path, const char *source,
                             const char *uuid, const char *cloud_job_id,
                             float bed_target,
                             float head_target, char *out, size_t out_sz)
{
    size_t pos = 0;
    char tmp[96];

    if (!path || !*path || !out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (append(out, out_sz, &pos, DENEB_COMMAND_VERB_JOB "<{\"file\":\"") != 0 ||
        append_escaped(out, out_sz, &pos, path) != 0 ||
        append(out, out_sz, &pos, "\"") != 0)
        return -1;

    if (source && *source) {
        if (append(out, out_sz, &pos, ",\"source\":\"") != 0 ||
            append_escaped(out, out_sz, &pos, source) != 0 ||
            append(out, out_sz, &pos, "\"") != 0)
            return -1;
    }

    if (uuid && *uuid) {
        if (append(out, out_sz, &pos, ",\"uuid\":\"") != 0 ||
            append_escaped(out, out_sz, &pos, uuid) != 0 ||
            append(out, out_sz, &pos, "\"") != 0)
            return -1;
    }

    if (cloud_job_id && *cloud_job_id) {
        if (append(out, out_sz, &pos, ",\"cloud_job_id\":\"") != 0 ||
            append_escaped(out, out_sz, &pos, cloud_job_id) != 0 ||
            append(out, out_sz, &pos, "\"") != 0)
            return -1;
    }

    if (bed_target > 0.0f) {
        snprintf(tmp, sizeof(tmp), ",\"bedTset\":%.1f", bed_target);
        if (append(out, out_sz, &pos, tmp) != 0)
            return -1;
    }

    if (head_target > 0.0f) {
        snprintf(tmp, sizeof(tmp), ",\"headTset\":%.1f", head_target);
        if (append(out, out_sz, &pos, tmp) != 0)
            return -1;
    }

    if (append(out, out_sz, &pos, "}") != 0)
        return -1;
    return (int)pos;
}

int deneb_command_format_action(const char *verb, char *out, size_t out_sz)
{
    return deneb_command_format_raw(verb, "{}", out, out_sz);
}

int deneb_command_format_raw(const char *verb, const char *payload,
                             char *out, size_t out_sz)
{
    int n;

    if (!verb || !*verb || !out || out_sz == 0)
        return -1;
    n = snprintf(out, out_sz, "%s<%s", verb, payload ? payload : "{}");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_command_extract_job_path(const char *args_json, char *out,
                                   size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (!args_json)
        return -1;

    if (deneb_json_get_value(args_json, "path", out, out_sz) != 0 ||
        !out[0] || strcmp(out, DENEB_PRINT_NONE_VALUE) == 0) {
        if (deneb_json_get_value(args_json, "file", out, out_sz) != 0)
            return -1;
    }

    if (!out[0] || strcmp(out, DENEB_PRINT_NONE_VALUE) == 0) {
        out[0] = '\0';
        return -1;
    }

    return 0;
}

void deneb_command_frame_plan_init(deneb_command_frame_plan_t *plan)
{
    if (!plan)
        return;

    plan->len = -1;
    plan->has_job_path = 0;
    plan->job_path[0] = '\0';
}

static int is_empty_action_payload(const char *payload)
{
    return !payload || strcmp(payload, "{}") == 0;
}

static int is_simple_action_verb(const char *verb)
{
    return strcmp(verb, DENEB_COMMAND_VERB_ABORT) == 0 ||
           strcmp(verb, DENEB_COMMAND_VERB_PAUSE) == 0 ||
           strcmp(verb, DENEB_COMMAND_VERB_RESUME) == 0;
}

int deneb_command_plan_frame(const char *verb,
                             const char *payload,
                             char *out,
                             size_t out_sz,
                             deneb_command_frame_plan_t *plan)
{
    int len;

    if (plan)
        deneb_command_frame_plan_init(plan);

    if (!verb || !*verb || !out || out_sz == 0)
        return -1;

    if (strcmp(verb, DENEB_COMMAND_VERB_JOB) == 0 && payload && plan &&
        deneb_command_extract_job_path(payload, plan->job_path,
                                       sizeof(plan->job_path)) == 0) {
        plan->has_job_path = 1;
    }

    if (is_simple_action_verb(verb) && is_empty_action_payload(payload))
        len = deneb_command_format_action(verb, out, out_sz);
    else
        len = deneb_command_format_raw(verb, payload, out, out_sz);

    if (plan)
        plan->len = len;
    return len;
}
