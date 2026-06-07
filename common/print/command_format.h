/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMAND_FORMAT_H
#define DENEB_COMMAND_FORMAT_H

#include <stddef.h>

#define DENEB_COMMAND_VERB_GCODE "GCODE"
#define DENEB_COMMAND_VERB_MACRO "MACRO"
#define DENEB_COMMAND_VERB_JOB "JOB"
#define DENEB_COMMAND_VERB_ABORT "ABORT"
#define DENEB_COMMAND_VERB_PAUSE "PAUSE"
#define DENEB_COMMAND_VERB_RESUME "RESUME"

int deneb_command_format_gcode(const char *const *lines, size_t count,
                               char *out, size_t out_sz);
int deneb_command_format_macro(const char *macro, char *out, size_t out_sz);
int deneb_command_format_job(const char *path, const char *source,
                             const char *uuid, float bed_target,
                             float head_target, char *out, size_t out_sz);
int deneb_command_format_action(const char *verb, char *out, size_t out_sz);

#endif
