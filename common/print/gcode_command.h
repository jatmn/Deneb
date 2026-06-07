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
#define DENEB_GCODE_WAIT_FOR_MOVES "M400"
#define DENEB_GCODE_DISABLE_ALL_STEPPERS "M84"
#define DENEB_GCODE_DISABLE_EXTRUDER_STEPPER "M84 E"
#define DENEB_GCODE_FRAME_LIGHT_MAX_PWM 255
#define DENEB_GCODE_MAX_JOG_DISTANCE_MM 50.0f
#define DENEB_GCODE_MAX_POSITION_X_MM 223.0f
#define DENEB_GCODE_MAX_POSITION_Y_MM 220.0f
#define DENEB_GCODE_MAX_POSITION_Z_MM 205.0f
#define DENEB_GCODE_DEFAULT_MOVE_SPEED_MM_S 150.0f
#define DENEB_GCODE_MAX_MOVE_SPEED_MM_S 300.0f

int deneb_gcode_axis_is_motion_axis(char axis);
char deneb_gcode_normalize_motion_axis(char axis);
int deneb_gcode_valid_jog_distance(float distance);
int deneb_gcode_valid_position_value(char axis, float value);
int deneb_gcode_valid_move_speed(float speed_mm_s);
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
int deneb_gcode_format_nozzle_off(char *out, size_t out_sz);
int deneb_gcode_format_bed_off(char *out, size_t out_sz);
int deneb_gcode_frame_light_brightness_to_pwm(int brightness_percent);
int deneb_gcode_format_frame_light(int brightness_percent,
                                   char *out, size_t out_sz);

#endif
