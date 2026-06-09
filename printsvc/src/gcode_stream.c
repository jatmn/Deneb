/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "gcode_rewrite.h"
#include "gcode_stream.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int deneb_gcode_stream_open(deneb_gcode_stream_t *stream, const char *path)
{
    if (!stream || !path || !*path)
        return -1;

    memset(stream, 0, sizeof(*stream));
    stream->file = fopen(path, "rb");
    if (!stream->file)
        return -1;

    strncpy(stream->path, path, sizeof(stream->path) - 1);
    return 0;
}

int deneb_gcode_stream_next(deneb_gcode_stream_t *stream, char *line, size_t line_sz)
{
    char *p;
    size_t len;
    deneb_gcode_rewrite_t rewrite;

    if (!stream || !stream->file || !line || line_sz == 0)
        return -1;

    stream->last_wait_for_bed = 0;
    stream->last_wait_for_nozzle = 0;
    stream->last_wait_target = 0.0f;

    if (stream->pending_index < stream->pending_count) {
        snprintf(line, line_sz, "%s", stream->pending[stream->pending_index++]);
        if (stream->pending_index >= stream->pending_count) {
            stream->pending_count = 0;
            stream->pending_index = 0;
        }
        return 1;
    }

    while (fgets(line, (int)line_sz, stream->file)) {
        stream->line_number++;
        line[line_sz - 1] = '\0';

        p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == ';' || *p == '\0')
            continue;

        char *comment = strchr(p, ';');
        if (comment)
            *comment = '\0';

        len = strlen(p);
        while (len > 0 && isspace((unsigned char)p[len - 1]))
            p[--len] = '\0';
        if (len == 0)
            continue;

        if (p != line)
            memmove(line, p, len + 1);
        if (deneb_gcode_rewrite_line(line, &rewrite) < 0)
            return -1;
        if (rewrite.count == 0)
            continue;
        stream->last_wait_for_bed = rewrite.wait_for_bed;
        stream->last_wait_for_nozzle = rewrite.wait_for_nozzle;
        stream->last_wait_target = rewrite.wait_target;
        snprintf(line, line_sz, "%s", rewrite.commands[0]);
        if (rewrite.count > 1) {
            stream->pending_count = rewrite.count - 1;
            stream->pending_index = 0;
            for (size_t i = 1; i < rewrite.count; i++) {
                snprintf(stream->pending[i - 1], sizeof(stream->pending[i - 1]),
                         "%s", rewrite.commands[i]);
            }
        }
        return 1;
    }

    return 0;
}

int deneb_gcode_stream_last_wait(const deneb_gcode_stream_t *stream,
                                 int *wait_for_bed,
                                 int *wait_for_nozzle,
                                 float *target)
{
    if (!stream || !wait_for_bed || !wait_for_nozzle || !target)
        return -1;
    *wait_for_bed = stream->last_wait_for_bed;
    *wait_for_nozzle = stream->last_wait_for_nozzle;
    *target = stream->last_wait_target;
    return *wait_for_bed || *wait_for_nozzle;
}

void deneb_gcode_stream_close(deneb_gcode_stream_t *stream)
{
    if (!stream)
        return;
    if (stream->file) {
        fclose(stream->file);
        stream->file = NULL;
    }
}
