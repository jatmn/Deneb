/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Printer endpoint implementations. Maps printer_state_t to UM API v1 format.
 */

#include "api_printer.h"
#include "backend_zmq.h"
#include "json_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (void)req;
    (void)resp;
    /* M7: Move head. Requires safety checks. */
    resp->status_code = 501;
    api_http_set_body_str(resp, "{\"message\":\"Not implemented\"}");
}

void api_printer_position_post(const http_request_t *req, http_response_t *resp)
{
    /* Home command */
    const char *p = strstr(req->body, "\"home\"");
    if (p) {
        if (backend_zmq_send_gcode("G28") < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to home printer\"}");
            return;
        }
        api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    } else {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Unknown action\"}");
    }
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
