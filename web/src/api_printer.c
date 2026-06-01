/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Printer endpoint implementations. Maps printer_state_t to UM API v1 format.
 */

#include "api_printer.h"
#include "backend_zmq.h"
#include "json_writer.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOG_DISTANCE_MM 50.0f
#define MAX_POSITION_X_MM 223.0f
#define MAX_POSITION_Y_MM 220.0f
#define MAX_POSITION_Z_MM 205.0f
#define DEFAULT_MOVE_SPEED_MM_S 150.0f
#define MAX_MOVE_SPEED_MM_S 300.0f

static int motion_allowed(const printer_state_t *s)
{
    return s && s->connected && !s->is_printing && !s->is_paused && !s->has_error;
}

static void set_motion_blocked(http_response_t *resp)
{
    resp->status_code = 409;
    api_http_set_body_str(resp, "{\"message\":\"Motion unavailable while printer is disconnected, printing, paused, or in error\"}");
}

static int parse_json_string_field(const char *body, const char *field, char *out, size_t out_sz)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", field);

    const char *p = strstr(body, search);
    if (!p) return -1;

    p = strchr(p + strlen(search), ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return -1;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    if (*p != '"') return -1;
    out[i] = '\0';
    return 0;
}

static int parse_json_float_field(const char *body, const char *field, float *value)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", field);

    const char *p = strstr(body, search);
    if (!p) return -1;

    p = strchr(p + strlen(search), ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    char *end = NULL;
    float parsed = strtof(p, &end);
    if (end == p) return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end && *end != ',' && *end != '}' && *end != ']') return -1;
    *value = parsed;
    return 0;
}

static int json_field_present(const char *body, const char *field)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", field);
    return strstr(body, search) != NULL;
}

static int parse_axis_value(const char *body, char *axis)
{
    char axis_buf[8];
    if (parse_json_string_field(body, "axis", axis_buf, sizeof(axis_buf)) < 0 || !axis_buf[0] || axis_buf[1])
        return -1;

    char upper = (char)toupper((unsigned char)axis_buf[0]);
    if (upper != 'X' && upper != 'Y' && upper != 'Z')
        return -1;

    *axis = upper;
    return 0;
}

static int valid_jog_distance(float distance)
{
    float mag = fabsf(distance);
    return isfinite(distance) &&
        (mag == 1.0f || mag == 10.0f || mag == MAX_JOG_DISTANCE_MM);
}

static int valid_position_value(char axis, float value)
{
    float max_value = 0.0f;
    if (axis == 'X') max_value = MAX_POSITION_X_MM;
    else if (axis == 'Y') max_value = MAX_POSITION_Y_MM;
    else if (axis == 'Z') max_value = MAX_POSITION_Z_MM;
    else return 0;

    return isfinite(value) && value >= 0.0f && value <= max_value;
}

static int send_jog_command(char axis, float distance)
{
    char move_cmd[64];
    if (backend_zmq_send_gcode("G91") < 0)
        return -1;
    snprintf(move_cmd, sizeof(move_cmd), "G1 %c%.3g F3000", axis, distance);
    if (backend_zmq_send_gcode(move_cmd) < 0) {
        backend_zmq_send_gcode("G90");
        return -1;
    }
    if (backend_zmq_send_gcode("G90") < 0)
        return -1;
    return 0;
}

static int send_absolute_position_command(int has_x, float x, int has_y, float y, int has_z, float z, float speed)
{
    char move_cmd[96];
    int len;

    if (backend_zmq_send_gcode("G90") < 0)
        return -1;

    len = snprintf(move_cmd, sizeof(move_cmd), "G1");
    if (has_x)
        len += snprintf(move_cmd + len, sizeof(move_cmd) - (size_t)len, " X%.3g", x);
    if (has_y)
        len += snprintf(move_cmd + len, sizeof(move_cmd) - (size_t)len, " Y%.3g", y);
    if (has_z)
        len += snprintf(move_cmd + len, sizeof(move_cmd) - (size_t)len, " Z%.3g", z);
    if (len < 0 || (size_t)len >= sizeof(move_cmd))
        return -1;
    snprintf(move_cmd + len, sizeof(move_cmd) - (size_t)len, " F%.3g", speed * 60.0f);

    return backend_zmq_send_gcode(move_cmd);
}

static int send_motion_action(const char *action)
{
    if (strcmp(action, "home") == 0) {
        return backend_zmq_send_command("MACRO", "{\"macro\":\"home_and_center_head.gcode\"}");
    }
    if (strcmp(action, "z_home") == 0) {
        return backend_zmq_send_gcode("G28 Z");
    }
    if (strcmp(action, "bed_up") == 0) {
        return backend_zmq_send_command("MACRO", "{\"macro\":\"move_buildplate_up.gcode\"}");
    }
    if (strcmp(action, "bed_down") == 0) {
        return backend_zmq_send_command("MACRO", "{\"macro\":\"move_buildplate_down.gcode\"}");
    }
    return -2;
}

static void set_motion_failed(http_response_t *resp)
{
    resp->status_code = 503;
    api_http_set_body_str(resp, "{\"message\":\"Failed to send motion command\"}");
}

static const char *get_status_string(const printer_state_t *s)
{
    if (s->has_error) return "error";
    if (s->is_paused) return "paused";
    if (s->is_printing) return "printing";
    return "idle";
}

void api_printer_status_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[32];
    snprintf(buf, sizeof(buf), "\"%s\"", get_status_string(s));
    api_http_set_body_str(resp, buf);
}

void api_printer_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[2048];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));

    json_obj_open(&w);

    /* bed */
    json_key(&w, "bed");
    json_obj_open(&w);
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->bed_temp_cur);
    json_float(&w, "target", s->bed_temp_set);
    json_obj_close(&w);
    json_str(&w, "type", "glass");
    json_key(&w, "pre_heat");
    json_obj_open(&w);
    json_bool(&w, "active", 0);
    json_obj_close(&w);
    json_obj_close(&w);

    /* heads */
    json_key(&w, "heads");
    json_arr_open(&w);
    json_obj_open(&w);
    json_int(&w, "acceleration", 3000);

    json_key(&w, "extruders");
    json_arr_open(&w);
    json_obj_open(&w);

    /* active_material */
    json_key(&w, "active_material");
    json_obj_open(&w);
    json_str(&w, "GUID", "");
    json_str(&w, "guid", "");
    json_float(&w, "length_remaining", -1.0);
    json_obj_close(&w);

    /* feeder */
    json_key(&w, "feeder");
    json_obj_open(&w);
    json_float(&w, "acceleration", 3000);
    json_float(&w, "jerk", 5.0);
    json_float(&w, "max_speed", 45.0);
    json_obj_close(&w);

    /* hotend */
    json_key(&w, "hotend");
    json_obj_open(&w);
    json_str(&w, "id", "AA+ 0.4");
    json_str(&w, "serial", "");
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->nozzle_temp_cur);
    json_float(&w, "target", s->nozzle_temp_set);
    json_obj_close(&w);
    json_key(&w, "offset");
    json_obj_open(&w);
    json_float(&w, "x", 0);
    json_float(&w, "y", 0);
    json_float(&w, "z", 0);
    json_str(&w, "state", "valid");
    json_obj_close(&w);
    json_key(&w, "statistics");
    json_obj_open(&w);
    json_str(&w, "last_material_guid", "");
    json_int(&w, "material_extruded", 0);
    json_int(&w, "max_temperature_exposed", 0);
    json_int(&w, "time_spent_hot", 0);
    json_obj_close(&w);
    json_obj_close(&w);

    json_obj_close(&w); /* extruder */
    json_arr_close(&w); /* extruders */

    json_key(&w, "position");
    json_obj_open(&w);
    json_float(&w, "x", s->pos_x);
    json_float(&w, "y", s->pos_y);
    json_float(&w, "z", s->pos_z);
    json_obj_close(&w);

    json_int(&w, "fan", 0);

    json_key(&w, "jerk");
    json_obj_open(&w);
    json_float(&w, "x", 20.0);
    json_float(&w, "y", 20.0);
    json_float(&w, "z", 1.0);
    json_obj_close(&w);

    json_key(&w, "max_speed");
    json_obj_open(&w);
    json_float(&w, "x", 300.0);
    json_float(&w, "y", 300.0);
    json_float(&w, "z", 40.0);
    json_obj_close(&w);

    json_obj_close(&w); /* head */
    json_arr_close(&w); /* heads */

    /* status */
    json_str(&w, "status", get_status_string(s));
    json_bool(&w, "connected", s->connected);
    json_bool(&w, "is_printing", s->is_printing);
    json_bool(&w, "is_paused", s->is_paused);
    json_bool(&w, "has_error", s->has_error);

    /* diagnostics */
    json_key(&w, "diagnostics");
    json_obj_open(&w);
    json_obj_close(&w);

    json_obj_close(&w); /* root */
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_bed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[256];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->bed_temp_cur);
    json_float(&w, "target", s->bed_temp_set);
    json_obj_close(&w);
    json_str(&w, "type", "glass");
    json_key(&w, "pre_heat");
    json_obj_open(&w);
    json_bool(&w, "active", 0);
    json_obj_close(&w);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_bed_temp_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[128];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_float(&w, "current", s->bed_temp_cur);
    json_float(&w, "target", s->bed_temp_set);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_bed_type_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"glass\"");
}

void api_printer_bed_preheat_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "{\"active\":false}");
}

void api_printer_heads_get(const http_request_t *req, http_response_t *resp)
{
    /* Return heads as an array with single element */
    const printer_state_t *s = backend_zmq_get_state();
    char buf[640];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_int(&w, "acceleration", 3000);
    json_key(&w, "position");
    json_obj_open(&w);
    json_float(&w, "x", s->pos_x);
    json_float(&w, "y", s->pos_y);
    json_float(&w, "z", s->pos_z);
    json_obj_close(&w);
    json_int(&w, "fan", 0);
    json_key(&w, "jerk");
    json_obj_open(&w);
    json_float(&w, "x", 20.0);
    json_float(&w, "y", 20.0);
    json_float(&w, "z", 1.0);
    json_obj_close(&w);
    json_key(&w, "max_speed");
    json_obj_open(&w);
    json_float(&w, "x", 300.0);
    json_float(&w, "y", 300.0);
    json_float(&w, "z", 40.0);
    json_obj_close(&w);
    json_obj_close(&w);
    json_arr_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_head_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_int(&w, "acceleration", 3000);
    json_key(&w, "position");
    json_obj_open(&w);
    json_float(&w, "x", s->pos_x);
    json_float(&w, "y", s->pos_y);
    json_float(&w, "z", s->pos_z);
    json_obj_close(&w);
    json_int(&w, "fan", 0);
    json_key(&w, "jerk");
    json_obj_open(&w);
    json_float(&w, "x", 20.0);
    json_float(&w, "y", 20.0);
    json_float(&w, "z", 1.0);
    json_obj_close(&w);
    json_key(&w, "max_speed");
    json_obj_open(&w);
    json_float(&w, "x", 300.0);
    json_float(&w, "y", 300.0);
    json_float(&w, "z", 40.0);
    json_obj_close(&w);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_position_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[128];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_float(&w, "x", s->pos_x);
    json_float(&w, "y", s->pos_y);
    json_float(&w, "z", s->pos_z);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_extruders_get(const http_request_t *req, http_response_t *resp)
{
    /* Return extruders as an array with single element */
    const printer_state_t *s = backend_zmq_get_state();
    char buf[640];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_key(&w, "active_material");
    json_obj_open(&w);
    json_str(&w, "GUID", "");
    json_str(&w, "guid", "");
    json_float(&w, "length_remaining", -1.0);
    json_obj_close(&w);
    json_key(&w, "feeder");
    json_obj_open(&w);
    json_float(&w, "acceleration", 3000);
    json_float(&w, "jerk", 5.0);
    json_float(&w, "max_speed", 45.0);
    json_obj_close(&w);
    json_key(&w, "hotend");
    json_obj_open(&w);
    json_str(&w, "id", "AA+ 0.4");
    json_str(&w, "serial", "");
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->nozzle_temp_cur);
    json_float(&w, "target", s->nozzle_temp_set);
    json_obj_close(&w);
    json_obj_close(&w);
    json_obj_close(&w);
    json_arr_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_extruder_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);

    json_key(&w, "active_material");
    json_obj_open(&w);
    json_str(&w, "GUID", "");
    json_str(&w, "guid", "");
    json_float(&w, "length_remaining", -1.0);
    json_obj_close(&w);

    json_key(&w, "feeder");
    json_obj_open(&w);
    json_float(&w, "acceleration", 3000);
    json_float(&w, "jerk", 5.0);
    json_float(&w, "max_speed", 45.0);
    json_obj_close(&w);

    json_key(&w, "hotend");
    json_obj_open(&w);
    json_str(&w, "id", "AA+ 0.4");
    json_str(&w, "serial", "");
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->nozzle_temp_cur);
    json_float(&w, "target", s->nozzle_temp_set);
    json_obj_close(&w);
    json_obj_close(&w);

    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_hotend_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[384];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "id", "AA+ 0.4");
    json_str(&w, "serial", "");
    json_key(&w, "temperature");
    json_obj_open(&w);
    json_float(&w, "current", s->nozzle_temp_cur);
    json_float(&w, "target", s->nozzle_temp_set);
    json_obj_close(&w);
    json_key(&w, "offset");
    json_obj_open(&w);
    json_float(&w, "x", 0);
    json_float(&w, "y", 0);
    json_float(&w, "z", 0);
    json_str(&w, "state", "valid");
    json_obj_close(&w);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_hotend_temp_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[128];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_float(&w, "current", s->nozzle_temp_cur);
    json_float(&w, "target", s->nozzle_temp_set);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_feeder_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_float(&w, "acceleration", 3000);
    json_float(&w, "jerk", 5.0);
    json_float(&w, "max_speed", 45.0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_material_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "{\"GUID\":\"\",\"guid\":\"\",\"length_remaining\":-1}");
}

void api_printer_led_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_float(&w, "hue", 0.0);
    json_float(&w, "saturation", 0.0);
    json_float(&w, "brightness", 100.0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_led_brightness_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "100");
}

void api_printer_led_hue_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "0");
}

void api_printer_led_saturation_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "0");
}

void api_printer_network_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    /* Basic network info - read from system */
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_key(&w, "wifi");
    json_obj_open(&w);
    json_str(&w, "ssid", "");
    json_str(&w, "security", "");
    json_int(&w, "strength", 0);
    json_str(&w, "state", "not_connected");
    json_obj_close(&w);
    json_key(&w, "ethernet");
    json_obj_open(&w);
    json_str(&w, "state", "not_connected");
    json_str(&w, "ip_address", "");
    json_obj_close(&w);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_printer_ambient_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "{\"current\":0.0}");
}

void api_printer_airmanager_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[256];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "firmware_version", "");
    json_int(&w, "filter_age", 0);
    json_int(&w, "filter_max_age", 0);
    json_str(&w, "filter_status", "none");
    json_str(&w, "status", s->topcap_present ? "connected" : "not_connected");
    json_int(&w, "fan_speed", 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

/* ========== M7 Write Endpoints ========== */

void api_printer_bed_temp_put(const http_request_t *req, http_response_t *resp)
{
    /* Parse temperature from body: {"temperature": N} or form data */
    float temp = 0;
    const char *p = strstr(req->body, "\"temperature\"");
    if (p) {
        p = strchr(p + 13, ':');
        if (p) temp = strtof(p + 1, NULL);
    }
    if (temp > 110) temp = 110;
    if (temp < 0) temp = 0;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "M140 S%.0f", temp);
    if (backend_zmq_send_gcode(cmd) < 0) {
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to set temperature\"}");
        return;
    }
    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_printer_hotend_temp_put(const http_request_t *req, http_response_t *resp)
{
    float temp = 0;
    const char *p = strstr(req->body, "\"temperature\"");
    if (p) {
        p = strchr(p + 13, ':');
        if (p) temp = strtof(p + 1, NULL);
    }
    if (temp > 260) temp = 260;
    if (temp < 0) temp = 0;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "M104 S%.0f", temp);
    if (backend_zmq_send_gcode(cmd) < 0) {
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to set temperature\"}");
        return;
    }
    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_printer_bed_preheat_put(const http_request_t *req, http_response_t *resp)
{
    /* Delegate to bed temp */
    api_printer_bed_temp_put(req, resp);
}

void api_printer_position_put(const http_request_t *req, http_response_t *resp)
{
    const printer_state_t *s = backend_zmq_get_state();
    if (!motion_allowed(s)) {
        set_motion_blocked(resp);
        return;
    }

    char axis;
    float distance = 0;
    if (json_field_present(req->body, "axis") || json_field_present(req->body, "distance")) {
        if (parse_axis_value(req->body, &axis) < 0 ||
            parse_json_float_field(req->body, "distance", &distance) < 0) {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number}\"}");
            return;
        }

        if (!valid_jog_distance(distance)) {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Distance must be one of -50, -10, -1, 1, 10, or 50\"}");
            return;
        }

        if (send_jog_command(axis, distance) < 0) {
            set_motion_failed(resp);
            return;
        }

        api_http_set_body_str(resp, "{\"message\":\"OK\"}");
        return;
    }

    float x = 0;
    float y = 0;
    float z = 0;
    float speed = DEFAULT_MOVE_SPEED_MM_S;
    int has_x = parse_json_float_field(req->body, "x", &x) == 0;
    int has_y = parse_json_float_field(req->body, "y", &y) == 0;
    int has_z = parse_json_float_field(req->body, "z", &z) == 0;

    if (!has_x && json_field_present(req->body, "x")) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid x position\"}");
        return;
    }
    if (!has_y && json_field_present(req->body, "y")) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid y position\"}");
        return;
    }
    if (!has_z && json_field_present(req->body, "z")) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid z position\"}");
        return;
    }
    if (!has_x && !has_y && !has_z) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Expected jog {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number} or position {\\\"x\\\":number,\\\"y\\\":number,\\\"z\\\":number}\"}");
        return;
    }
    if ((has_x && !valid_position_value('X', x)) ||
        (has_y && !valid_position_value('Y', y)) ||
        (has_z && !valid_position_value('Z', z))) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Position is outside the printable volume\"}");
        return;
    }
    if (json_field_present(req->body, "speed") &&
        (parse_json_float_field(req->body, "speed", &speed) < 0 ||
         !isfinite(speed) || speed <= 0.0f || speed > MAX_MOVE_SPEED_MM_S)) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid movement speed\"}");
        return;
    }
    if (send_absolute_position_command(has_x, x, has_y, y, has_z, z, speed) < 0) {
        set_motion_failed(resp);
        return;
    }
    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_printer_position_post(const http_request_t *req, http_response_t *resp)
{
    const printer_state_t *s = backend_zmq_get_state();
    if (!motion_allowed(s)) {
        set_motion_blocked(resp);
        return;
    }

    char action[32];
    if (parse_json_string_field(req->body, "action", action, sizeof(action)) < 0) {
        if (strstr(req->body, "\"home\"")) {
            snprintf(action, sizeof(action), "home");
        } else {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"action\\\":\\\"home|z_home|bed_up|bed_down\\\"}\"}");
            return;
        }
    }

    int rc = send_motion_action(action);
    if (rc == -2) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Unknown motion action\"}");
        return;
    }
    if (rc < 0) {
        set_motion_failed(resp);
        return;
    }

    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_printer_led_put(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    (void)resp;
    resp->status_code = 501;
    api_http_set_body_str(resp, "{\"message\":\"Not implemented\"}");
}

void api_printer_led_brightness_put(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    (void)resp;
    resp->status_code = 501;
    api_http_set_body_str(resp, "{\"message\":\"Not implemented\"}");
}

void api_printer_validate_post(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    /* Stub: always valid */
    api_http_set_body_str(resp, "{\"valid\":true}");
}
