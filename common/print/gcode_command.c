/* SPDX-License-Identifier: MPL-2.0 */
#include "gcode_command.h"

#include "json_field.h"

#include <math.h>
#include <stdio.h>

char deneb_gcode_normalize_motion_axis(char axis)
{
    if (axis >= 'a' && axis <= 'z')
        axis = (char)(axis - ('a' - 'A'));
    return axis;
}

int deneb_gcode_axis_is_motion_axis(char axis)
{
    axis = deneb_gcode_normalize_motion_axis(axis);
    return axis == 'X' || axis == 'Y' || axis == 'Z';
}

int deneb_gcode_valid_jog_distance(float distance)
{
    float mag = fabsf(distance);
    int whole;

    if (!isfinite(distance) ||
        mag < 1.0f ||
        mag > DENEB_GCODE_MAX_JOG_DISTANCE_MM)
        return 0;

    whole = (int)mag;
    return mag == (float)whole;
}

int deneb_gcode_valid_position_value(char axis, float value)
{
    float max_value = 0.0f;

    switch (deneb_gcode_normalize_motion_axis(axis)) {
        case 'X':
            max_value = DENEB_GCODE_MAX_POSITION_X_MM;
            break;
        case 'Y':
            max_value = DENEB_GCODE_MAX_POSITION_Y_MM;
            break;
        case 'Z':
            max_value = DENEB_GCODE_MAX_POSITION_Z_MM;
            break;
        default:
            return 0;
    }

    return isfinite(value) && value >= 0.0f && value <= max_value;
}

int deneb_gcode_valid_move_speed(float speed_mm_s)
{
    return isfinite(speed_mm_s) &&
           speed_mm_s > 0.0f &&
           speed_mm_s <= DENEB_GCODE_MAX_MOVE_SPEED_MM_S;
}

int deneb_gcode_format_jog(char axis, float distance, char *out, size_t out_sz)
{
    axis = deneb_gcode_normalize_motion_axis(axis);
    if (!deneb_gcode_axis_is_motion_axis(axis) || !out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "G1 %c%.3g F3000", axis, distance) >=
        (int)out_sz)
        return -1;
    return 0;
}

int deneb_gcode_build_jog_sequence(char axis, float distance,
                                   deneb_gcode_jog_sequence_t *seq)
{
    if (!seq)
        return -1;
    if (deneb_gcode_format_jog(axis, distance, seq->move,
                               sizeof(seq->move)) < 0)
        return -1;

    seq->lines[0] = DENEB_GCODE_RELATIVE_MODE;
    seq->lines[1] = seq->move;
    seq->lines[2] = DENEB_GCODE_ABSOLUTE_MODE;
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

int deneb_gcode_build_material_move_sequence(
    int unload, deneb_gcode_material_move_sequence_t *seq)
{
    float distance = unload ? -DENEB_GCODE_MATERIAL_MOVE_DISTANCE_MM :
                              DENEB_GCODE_MATERIAL_MOVE_DISTANCE_MM;
    float feedrate = unload ? DENEB_GCODE_MATERIAL_UNLOAD_FEEDRATE_MM_MIN :
                              DENEB_GCODE_MATERIAL_LOAD_FEEDRATE_MM_MIN;

    if (!seq)
        return -1;
    if (deneb_gcode_format_extrude(distance, feedrate, seq->move,
                                   sizeof(seq->move)) < 0)
        return -1;

    seq->lines[0] = DENEB_GCODE_RESET_EXTRUDER;
    seq->lines[1] = seq->move;
    seq->duration_ms =
        (uint32_t)((DENEB_GCODE_MATERIAL_MOVE_DISTANCE_MM * 60000.0f) /
                   feedrate) +
        DENEB_GCODE_MATERIAL_MOVE_MARGIN_MS;
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

void deneb_gcode_motion_plan_init(deneb_gcode_motion_plan_t *plan)
{
    if (!plan)
        return;

    plan->kind = DENEB_GCODE_MOTION_PLAN_NONE;
    plan->jog.move[0] = '\0';
    plan->jog.lines[0] = NULL;
    plan->jog.lines[1] = NULL;
    plan->jog.lines[2] = NULL;
    plan->absolute_move[0] = '\0';
    plan->absolute_lines[0] = NULL;
    plan->absolute_lines[1] = NULL;
}

static int parse_motion_axis_json(const char *json, char *axis)
{
    char axis_buf[8];
    char upper;

    if (!axis ||
        deneb_json_get_value(json, "axis", axis_buf, sizeof(axis_buf)) < 0 ||
        !axis_buf[0] ||
        axis_buf[1])
        return -1;

    upper = deneb_gcode_normalize_motion_axis(axis_buf[0]);
    if (!deneb_gcode_axis_is_motion_axis(upper))
        return -1;

    *axis = upper;
    return 0;
}

static int plan_jog_motion_from_json(const char *json,
                                     deneb_gcode_motion_plan_t *plan)
{
    char axis;
    float distance = 0.0f;

    if (parse_motion_axis_json(json, &axis) < 0 ||
        deneb_json_get_float_value(json, "distance", &distance) < 0)
        return DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE;

    if (!deneb_gcode_valid_jog_distance(distance))
        return DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE;

    if (deneb_gcode_build_jog_sequence(axis, distance, &plan->jog) < 0)
        return DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE;

    plan->kind = DENEB_GCODE_MOTION_PLAN_JOG;
    return DENEB_GCODE_MOTION_PLAN_OK;
}

int deneb_gcode_plan_motion_from_json(const char *json,
                                      deneb_gcode_motion_plan_t *plan)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float speed = DENEB_GCODE_DEFAULT_MOVE_SPEED_MM_S;
    int has_x;
    int has_y;
    int has_z;

    if (!plan)
        return DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED;

    deneb_gcode_motion_plan_init(plan);

    if (deneb_json_field_present(json, "axis") ||
        deneb_json_field_present(json, "distance"))
        return plan_jog_motion_from_json(json, plan);

    has_x = deneb_json_get_float_value(json, "x", &x) == 0;
    has_y = deneb_json_get_float_value(json, "y", &y) == 0;
    has_z = deneb_json_get_float_value(json, "z", &z) == 0;

    if (!has_x && deneb_json_field_present(json, "x"))
        return DENEB_GCODE_MOTION_PLAN_ERR_X;
    if (!has_y && deneb_json_field_present(json, "y"))
        return DENEB_GCODE_MOTION_PLAN_ERR_Y;
    if (!has_z && deneb_json_field_present(json, "z"))
        return DENEB_GCODE_MOTION_PLAN_ERR_Z;
    if (!has_x && !has_y && !has_z)
        return DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED;

    if ((has_x && !deneb_gcode_valid_position_value('X', x)) ||
        (has_y && !deneb_gcode_valid_position_value('Y', y)) ||
        (has_z && !deneb_gcode_valid_position_value('Z', z)))
        return DENEB_GCODE_MOTION_PLAN_ERR_VOLUME;

    if (deneb_json_field_present(json, "speed") &&
        (deneb_json_get_float_value(json, "speed", &speed) < 0 ||
         !deneb_gcode_valid_move_speed(speed)))
        return DENEB_GCODE_MOTION_PLAN_ERR_SPEED;

    if (deneb_gcode_format_absolute_position(
            has_x, x, has_y, y, has_z, z, speed, plan->absolute_move,
            sizeof(plan->absolute_move)) < 0)
        return DENEB_GCODE_MOTION_PLAN_ERR_VOLUME;

    plan->kind = DENEB_GCODE_MOTION_PLAN_ABSOLUTE_POSITION;
    plan->absolute_lines[0] = DENEB_GCODE_ABSOLUTE_MODE;
    plan->absolute_lines[1] = plan->absolute_move;
    return DENEB_GCODE_MOTION_PLAN_OK;
}

const char *deneb_gcode_motion_plan_error_response(int rc)
{
    switch (rc) {
        case DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE:
            return "{\"message\":\"Expected {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number}\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE:
            return "{\"message\":\"Distance must be a whole number from 1 to 50 mm\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_X:
            return "{\"message\":\"Invalid x position\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_Y:
            return "{\"message\":\"Invalid y position\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_Z:
            return "{\"message\":\"Invalid z position\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_VOLUME:
            return "{\"message\":\"Position is outside the printable volume\"}";
        case DENEB_GCODE_MOTION_PLAN_ERR_SPEED:
            return "{\"message\":\"Invalid movement speed\"}";
        default:
            return "{\"message\":\"Expected jog {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number} or position {\\\"x\\\":number,\\\"y\\\":number,\\\"z\\\":number}\"}";
    }
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

int deneb_gcode_format_heater_target(deneb_gcode_heater_t heater,
                                     float temp_c,
                                     char *out,
                                     size_t out_sz)
{
    switch (heater) {
        case DENEB_GCODE_HEATER_NOZZLE:
            return deneb_gcode_format_nozzle_target(temp_c, out, out_sz);
        case DENEB_GCODE_HEATER_BED:
            return deneb_gcode_format_bed_target(temp_c, out, out_sz);
        default:
            return -1;
    }
}

static float heater_max_temp(deneb_gcode_heater_t heater)
{
    switch (heater) {
        case DENEB_GCODE_HEATER_NOZZLE:
            return DENEB_GCODE_MAX_NOZZLE_TEMP_C;
        case DENEB_GCODE_HEATER_BED:
            return DENEB_GCODE_MAX_BED_TEMP_C;
        default:
            return -1.0f;
    }
}

int deneb_gcode_plan_temperature_target_from_json(
    deneb_gcode_heater_t heater,
    const char *json,
    float *out_temp_c,
    char *out_gcode,
    size_t out_gcode_sz)
{
    float temp = 0.0f;
    float max_temp = heater_max_temp(heater);
    const char *temp_key = NULL;

    if (max_temp < 0.0f || !out_temp_c || !out_gcode || out_gcode_sz == 0)
        return -1;

    if (deneb_json_field_present(json, "temperature"))
        temp_key = "temperature";
    else if (deneb_json_field_present(json, "target"))
        temp_key = "target";

    if (temp_key) {
        if (deneb_json_get_float_value(json, temp_key, &temp) < 0 ||
            !isfinite(temp))
            return -1;
    }

    if (temp > max_temp)
        temp = max_temp;
    if (temp < 0.0f)
        temp = 0.0f;

    *out_temp_c = temp;
    return deneb_gcode_format_heater_target(heater, temp, out_gcode,
                                            out_gcode_sz);
}

const char *deneb_gcode_temperature_target_error_response(void)
{
    return "{\"message\":\"Invalid temperature\"}";
}

int deneb_gcode_format_nozzle_off(char *out, size_t out_sz)
{
    return deneb_gcode_format_nozzle_target(0.0f, out, out_sz);
}

int deneb_gcode_format_bed_off(char *out, size_t out_sz)
{
    return deneb_gcode_format_bed_target(0.0f, out, out_sz);
}

int deneb_gcode_build_cooldown_sequence(deneb_gcode_cooldown_sequence_t *seq)
{
    if (!seq)
        return -1;
    if (deneb_gcode_format_nozzle_off(seq->nozzle_off,
                                      sizeof(seq->nozzle_off)) < 0 ||
        deneb_gcode_format_bed_off(seq->bed_off, sizeof(seq->bed_off)) < 0)
        return -1;

    seq->lines[0] = seq->nozzle_off;
    seq->lines[1] = seq->bed_off;
    seq->lines[2] = DENEB_GCODE_FAN_OFF;
    return 0;
}

int deneb_gcode_frame_light_brightness_to_pwm(int brightness_percent)
{
    if (brightness_percent < 0)
        brightness_percent = 0;
    if (brightness_percent > 100)
        brightness_percent = 100;
    return (brightness_percent * DENEB_GCODE_FRAME_LIGHT_MAX_PWM + 50) / 100;
}

int deneb_gcode_format_frame_light(int brightness_percent,
                                   char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "M142 w%d",
                 deneb_gcode_frame_light_brightness_to_pwm(
                     brightness_percent)) >= (int)out_sz)
        return -1;
    return 0;
}

int deneb_gcode_format_air_manager_fan(int enabled, char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    if (snprintf(out, out_sz, "M12030 S%d",
                 enabled ? DENEB_GCODE_AIR_MANAGER_FAN_MAX_PWM : 0) >=
        (int)out_sz)
        return -1;
    return 0;
}
