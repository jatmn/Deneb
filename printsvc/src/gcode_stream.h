/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_GCODE_STREAM_H
#define DENEB_PRINTSVC_GCODE_STREAM_H

#include "config.h"

#include <stddef.h>

typedef struct {
    int fd;
    char path[256];
    char read_buf[512];
    size_t read_pos;
    size_t read_len;
    int eof;
    unsigned long line_number;
    int has_prime_cmd;
    char pending[DENEB_PRINTSVC_MAX_COMMANDS][DENEB_PRINTSVC_MAX_GCODE_LINE];
    size_t pending_count;
    size_t pending_index;
    int last_wait_for_bed;
    int last_wait_for_nozzle;
    float last_wait_target;
    int last_layer_zero;
    int last_time_elapsed_valid;
    float last_time_elapsed;
} deneb_gcode_stream_t;

int deneb_gcode_stream_open(deneb_gcode_stream_t *stream, const char *path);
int deneb_gcode_stream_has_file_prime_command(const char *path);
int deneb_gcode_stream_next(deneb_gcode_stream_t *stream, char *line, size_t line_sz);
int deneb_gcode_stream_last_wait(const deneb_gcode_stream_t *stream,
                                 int *wait_for_bed,
                                 int *wait_for_nozzle,
                                 float *target);
void deneb_gcode_stream_close(deneb_gcode_stream_t *stream);

#endif
