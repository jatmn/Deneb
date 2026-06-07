/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_GCODE_STREAM_H
#define DENEB_PRINTSVC_GCODE_STREAM_H

#include <stdio.h>
#include <stddef.h>

typedef struct {
    FILE *file;
    char path[256];
    unsigned long line_number;
} deneb_gcode_stream_t;

int deneb_gcode_stream_open(deneb_gcode_stream_t *stream, const char *path);
int deneb_gcode_stream_next(deneb_gcode_stream_t *stream, char *line, size_t line_sz);
void deneb_gcode_stream_close(deneb_gcode_stream_t *stream);

#endif
