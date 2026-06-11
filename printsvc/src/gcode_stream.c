/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "gcode_rewrite.h"
#include "gcode_stream.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int stream_read_char(deneb_gcode_stream_t *stream, char *ch)
{
    if (!stream || stream->fd < 0 || !ch)
        return -1;

    while (stream->read_pos >= stream->read_len) {
        ssize_t n;
        if (stream->eof)
            return 0;
        do {
            n = read(stream->fd, stream->read_buf,
                     sizeof(stream->read_buf));
        } while (n < 0 && errno == EINTR);
        if (n < 0)
            return -1;
        if (n == 0) {
            stream->eof = 1;
            return 0;
        }
        stream->read_pos = 0;
        stream->read_len = (size_t)n;
    }

    *ch = stream->read_buf[stream->read_pos++];
    return 1;
}

static int stream_read_line(deneb_gcode_stream_t *stream, char *line,
                            size_t line_sz)
{
    size_t len = 0;

    if (!stream || stream->fd < 0 || !line || line_sz == 0)
        return -1;

    line[0] = '\0';

    for (;;) {
        char ch;
        int rc = stream_read_char(stream, &ch);
        if (rc < 0)
            return -1;
        if (rc == 0)
            return len > 0 ? 1 : 0;
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            line[len] = '\0';
            return 1;
        }
        if (len + 1 < line_sz)
            line[len++] = ch;
    }
}

int deneb_gcode_stream_open(deneb_gcode_stream_t *stream, const char *path)
{
    if (!stream || !path || !*path)
        return -1;

    memset(stream, 0, sizeof(*stream));
    stream->fd = -1;
    stream->fd = open(path, O_RDONLY);
    if (stream->fd < 0)
        return -1;

    strncpy(stream->path, path, sizeof(stream->path) - 1);
    return 0;
}

int deneb_gcode_stream_next(deneb_gcode_stream_t *stream, char *line, size_t line_sz)
{
    char *p;
    size_t len;
    deneb_gcode_rewrite_t rewrite;

    if (!stream || stream->fd < 0 || !line || line_sz == 0)
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

    while (stream_read_line(stream, line, line_sz) > 0) {
        stream->line_number++;

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
    int fd;
    unsigned long line_number;

    if (!stream)
        return;
    fd = stream->fd;
    if (fd >= 0)
        close(fd);
    line_number = stream->line_number;
    memset(stream, 0, sizeof(*stream));
    stream->fd = -1;
    stream->line_number = line_number;
}
