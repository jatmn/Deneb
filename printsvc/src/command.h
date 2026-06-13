/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_COMMAND_H
#define DENEB_PRINTSVC_COMMAND_H

#include "config.h"

#include <stddef.h>

typedef enum {
    DENEB_COMMAND_UNKNOWN = 0,
    DENEB_COMMAND_GCODE,
    DENEB_COMMAND_MACRO,
    DENEB_COMMAND_JOB,
    DENEB_COMMAND_ABORT,
    DENEB_COMMAND_PAUSE,
    DENEB_COMMAND_RESUME
} deneb_command_type_t;

typedef struct {
    deneb_command_type_t type;
    char verb[16];
    char payload[512];
    char file[256];
    char source[32];
    char uuid[64];
    char cloud_job_id[96];
    char macro[128];
    float bed_target;
    float head_target;
    char gcode[DENEB_PRINTSVC_MAX_COMMANDS][DENEB_PRINTSVC_MAX_GCODE_LINE];
    size_t gcode_count;
} deneb_command_t;

const char *deneb_command_type_name(deneb_command_type_t type);
int deneb_command_parse(const char *frame, deneb_command_t *out);
int deneb_command_extract_json_string(const char *json, const char *key,
                                      char *out, size_t out_sz);

#endif
