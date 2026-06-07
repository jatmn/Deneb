/* SPDX-License-Identifier: MPL-2.0 */
#include "command_format.h"

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
                             const char *uuid, float bed_target,
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
    int n;

    if (!verb || !*verb || !out || out_sz == 0)
        return -1;
    n = snprintf(out, out_sz, "%s<{}", verb);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
