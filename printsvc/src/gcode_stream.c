/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "gcode_stream.h"

#include <ctype.h>
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

    if (!stream || !stream->file || !line || line_sz == 0)
        return -1;

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
        return 1;
    }

    return 0;
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
