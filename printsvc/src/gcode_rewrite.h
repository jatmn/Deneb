/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_GCODE_REWRITE_H
#define DENEB_PRINTSVC_GCODE_REWRITE_H

#include "config.h"

#include <stddef.h>

typedef struct {
    /* Only the first count commands are initialized. */
    char commands[DENEB_PRINTSVC_MAX_COMMANDS][DENEB_PRINTSVC_MAX_GCODE_LINE];
    size_t count;
    int wait_for_bed;
    int wait_for_nozzle;
    float wait_target;
} deneb_gcode_rewrite_t;

/* Returns 0 for an unchanged line, 1 for a handled line, or -1 for invalid input.
 * A handled line with count zero is suppressed. */
int deneb_gcode_rewrite_special(const char *line, deneb_gcode_rewrite_t *rewrite);
int deneb_gcode_rewrite_line(const char *line, deneb_gcode_rewrite_t *rewrite);

#endif
