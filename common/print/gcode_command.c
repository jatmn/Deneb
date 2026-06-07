/* SPDX-License-Identifier: MPL-2.0 */
#include "gcode_command.h"

#include <stdio.h>

static int valid_axis(char axis)
{
    return axis == 'X' || axis == 'Y' || axis == 'Z';
}

int deneb_gcode_format_jog(char axis, float distance, char *out, size_t out_sz)
{
    if (!valid_axis(axis) || !out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "G1 %c%.3g F3000", axis, distance) >=
        (int)out_sz)
        return -1;
    return 0;
}

int deneb_gcode_format_extrude(float distance, float feedrate,
                               char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "G1 E%.3g F%.3g", distance, feedrate) >=
        (int)out_sz)
        return -1;
    return 0;
}

int deneb_gcode_format_absolute_position(int has_x, float x,
                                         int has_y, float y,
                                         int has_z, float z,
                                         float speed_mm_s,
                                         char *out, size_t out_sz)
{
    int len;

    if (!out || out_sz == 0 || (!has_x && !has_y && !has_z))
        return -1;

    len = snprintf(out, out_sz, "G1");
    if (len < 0 || (size_t)len >= out_sz)
        return -1;
    if (has_x)
        len += snprintf(out + len, out_sz - (size_t)len, " X%.3g", x);
    if (len < 0 || (size_t)len >= out_sz)
        return -1;
    if (has_y)
        len += snprintf(out + len, out_sz - (size_t)len, " Y%.3g", y);
    if (len < 0 || (size_t)len >= out_sz)
        return -1;
    if (has_z)
        len += snprintf(out + len, out_sz - (size_t)len, " Z%.3g", z);
    if (len < 0 || (size_t)len >= out_sz)
        return -1;
    if (snprintf(out + len, out_sz - (size_t)len, " F%.3g",
                 speed_mm_s * 60.0f) >= (int)(out_sz - (size_t)len))
        return -1;
    return 0;
}

int deneb_gcode_format_nozzle_target(float temp_c, char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "M104 S%.0f", temp_c) >= (int)out_sz)
        return -1;
    return 0;
}

int deneb_gcode_format_bed_target(float temp_c, char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "M140 S%.0f", temp_c) >= (int)out_sz)
        return -1;
    return 0;
}
