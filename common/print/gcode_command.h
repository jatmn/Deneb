/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_GCODE_COMMAND_H
#define DENEB_COMMON_GCODE_COMMAND_H

#include <stddef.h>

#define DENEB_GCODE_ABSOLUTE_MODE "G90"
#define DENEB_GCODE_RELATIVE_MODE "G91"
#define DENEB_GCODE_HOME_Z "G28 Z"
#define DENEB_GCODE_FAN_OFF "M106 S0"
#define DENEB_GCODE_RESET_EXTRUDER "G92 E0"
#define DENEB_GCODE_STOP_MATERIAL "M401"

int deneb_gcode_format_jog(char axis, float distance, char *out, size_t out_sz);
int deneb_gcode_format_extrude(float distance, float feedrate,
                               char *out, size_t out_sz);
int deneb_gcode_format_absolute_position(int has_x, float x,
                                         int has_y, float y,
                                         int has_z, float z,
                                         float speed_mm_s,
                                         char *out, size_t out_sz);
int deneb_gcode_format_nozzle_target(float temp_c, char *out, size_t out_sz);
int deneb_gcode_format_bed_target(float temp_c, char *out, size_t out_sz);

#endif
