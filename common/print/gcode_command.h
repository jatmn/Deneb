/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_GCODE_COMMAND_H
#define DENEB_COMMON_GCODE_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#define DENEB_GCODE_ABSOLUTE_MODE "G90"
#define DENEB_GCODE_RELATIVE_MODE "G91"
#define DENEB_GCODE_HOME_Z "G28 Z"
#define DENEB_GCODE_FAN_OFF "M106 S0"
#define DENEB_GCODE_RESET_EXTRUDER "G92 E0"
#define DENEB_GCODE_STOP_MATERIAL "M401"
#define DENEB_GCODE_AIR_MANAGER_FAN_MAX_PWM 255
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
#define DENEB_GCODE_MAX_NOZZLE_TEMP_C 260.0f
#define DENEB_GCODE_MAX_BED_TEMP_C 110.0f
#define DENEB_GCODE_MATERIAL_MOVE_DISTANCE_MM 360.0f
#define DENEB_GCODE_MATERIAL_LOAD_FEEDRATE_MM_MIN 60.0f
#define DENEB_GCODE_MATERIAL_UNLOAD_FEEDRATE_MM_MIN 300.0f
#define DENEB_GCODE_MATERIAL_MOVE_MARGIN_MS 2000U

typedef struct {
    char move[64];
    const char *lines[3];
} deneb_gcode_jog_sequence_t;

typedef enum {
    DENEB_GCODE_MOTION_PLAN_NONE = 0,
    DENEB_GCODE_MOTION_PLAN_JOG,
    DENEB_GCODE_MOTION_PLAN_ABSOLUTE_POSITION
} deneb_gcode_motion_plan_kind_t;

typedef enum {
    DENEB_GCODE_MOTION_PLAN_OK = 0,
    DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED = -1,
    DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE = -2,
    DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE = -3,
    DENEB_GCODE_MOTION_PLAN_ERR_X = -4,
    DENEB_GCODE_MOTION_PLAN_ERR_Y = -5,
    DENEB_GCODE_MOTION_PLAN_ERR_Z = -6,
    DENEB_GCODE_MOTION_PLAN_ERR_VOLUME = -7,
    DENEB_GCODE_MOTION_PLAN_ERR_SPEED = -8
} deneb_gcode_motion_plan_result_t;

typedef struct {
    deneb_gcode_motion_plan_kind_t kind;
    deneb_gcode_jog_sequence_t jog;
    char absolute_move[96];
    const char *absolute_lines[2];
} deneb_gcode_motion_plan_t;

typedef struct {
    char nozzle_off[32];
    char bed_off[32];
    const char *lines[3];
} deneb_gcode_cooldown_sequence_t;

typedef enum {
    DENEB_GCODE_HEATER_NOZZLE = 0,
    DENEB_GCODE_HEATER_BED
} deneb_gcode_heater_t;

typedef struct {
    char move[64];
    const char *lines[2];
    uint32_t duration_ms;
} deneb_gcode_material_move_sequence_t;

int deneb_gcode_axis_is_motion_axis(char axis);
char deneb_gcode_normalize_motion_axis(char axis);
int deneb_gcode_valid_jog_distance(float distance);
int deneb_gcode_valid_position_value(char axis, float value);
int deneb_gcode_valid_move_speed(float speed_mm_s);
int deneb_gcode_format_jog(char axis, float distance, char *out, size_t out_sz);
int deneb_gcode_build_jog_sequence(char axis, float distance,
                                   deneb_gcode_jog_sequence_t *seq);
int deneb_gcode_format_extrude(float distance, float feedrate,
                               char *out, size_t out_sz);
int deneb_gcode_build_material_move_sequence(
    int unload, deneb_gcode_material_move_sequence_t *seq);
int deneb_gcode_format_absolute_position(int has_x, float x,
                                         int has_y, float y,
                                         int has_z, float z,
                                         float speed_mm_s,
                                         char *out, size_t out_sz);
void deneb_gcode_motion_plan_init(deneb_gcode_motion_plan_t *plan);
int deneb_gcode_plan_motion_from_json(const char *json,
                                      deneb_gcode_motion_plan_t *plan);
const char *deneb_gcode_motion_plan_error_response(int rc);
int deneb_gcode_format_nozzle_target(float temp_c, char *out, size_t out_sz);
int deneb_gcode_format_bed_target(float temp_c, char *out, size_t out_sz);
int deneb_gcode_format_heater_target(deneb_gcode_heater_t heater,
                                     float temp_c,
                                     char *out,
                                     size_t out_sz);
int deneb_gcode_plan_temperature_target_from_json(
    deneb_gcode_heater_t heater,
    const char *json,
    float *out_temp_c,
    char *out_gcode,
    size_t out_gcode_sz);
const char *deneb_gcode_temperature_target_error_response(void);
int deneb_gcode_format_nozzle_off(char *out, size_t out_sz);
int deneb_gcode_format_bed_off(char *out, size_t out_sz);
int deneb_gcode_build_cooldown_sequence(deneb_gcode_cooldown_sequence_t *seq);
int deneb_gcode_frame_light_brightness_to_pwm(int brightness_percent);
int deneb_gcode_format_frame_light(int brightness_percent,
                                   char *out, size_t out_sz);
int deneb_gcode_format_air_manager_fan(int enabled, char *out, size_t out_sz);

#endif
