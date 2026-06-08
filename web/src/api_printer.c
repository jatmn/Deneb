/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Printer endpoint implementations. Maps printer_state_t to UM API v1 format.
 */

#include "api_printer.h"
#include "backend_zmq.h"
#include "gcode_command.h"
#include "json_field.h"
#include "json_writer.h"
#include "manual_motion.h"
#include "printer_status_response.h"

#include <stdio.h>
#include <string.h>

static void set_motion_blocked(http_response_t *resp)
{
    resp->status_code = 409;
    api_http_set_body_str(resp, "{\"message\":\"Motion unavailable while printer is disconnected, printing, paused, or in error\"}");
}

static void set_motion_failed(http_response_t *resp)
{
    resp->status_code = 503;
    api_http_set_body_str(resp, "{\"message\":\"Failed to send motion command\"}");
}

static void set_motion_plan_error(http_response_t *resp, int rc)
{
    resp->status_code = 400;
    switch (rc) {
        case DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE:
            api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number}\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE:
            api_http_set_body_str(resp, "{\"message\":\"Distance must be a whole number from 1 to 50 mm\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_X:
            api_http_set_body_str(resp, "{\"message\":\"Invalid x position\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_Y:
            api_http_set_body_str(resp, "{\"message\":\"Invalid y position\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_Z:
            api_http_set_body_str(resp, "{\"message\":\"Invalid z position\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_VOLUME:
            api_http_set_body_str(resp, "{\"message\":\"Position is outside the printable volume\"}");
            break;
        case DENEB_GCODE_MOTION_PLAN_ERR_SPEED:
            api_http_set_body_str(resp, "{\"message\":\"Invalid movement speed\"}");
            break;
        default:
            api_http_set_body_str(resp, "{\"message\":\"Expected jog {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number} or position {\\\"x\\\":number,\\\"y\\\":number,\\\"z\\\":number}\"}");
            break;
    }
}

static int send_motion_plan(const deneb_gcode_motion_plan_t *plan)
{
    if (!plan)
        return -1;

    if (plan->kind == DENEB_GCODE_MOTION_PLAN_JOG)
        return backend_zmq_send_gcodes(plan->jog.lines, 3);

    if (plan->kind == DENEB_GCODE_MOTION_PLAN_ABSOLUTE_POSITION)
        return backend_zmq_send_gcodes(plan->absolute_lines, 2);

    return -1;
}

static void set_printer_response(http_response_t *resp,
                                 int rc,
                                 const char *body,
                                 const char *message)
{
    if (rc < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp, message);
        return;
    }
    api_http_set_body_str(resp, body);
}

void api_printer_status_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[32];
    deneb_printer_status_response_t status;
    backend_zmq_get_printer_status_response(&status);
    if (deneb_printer_status_response_format_status(&status, buf,
                                                    sizeof(buf)) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp,
                              "{\"message\":\"Printer status too large\"}");
        return;
    }
    api_http_set_body_str(resp, buf);
}

void api_printer_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[2048];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    if (deneb_printer_status_response_format_um_root(&status, buf,
                                                     sizeof(buf)) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp,
                              "{\"message\":\"Printer response too large\"}");
        return;
    }
    api_http_set_body_str(resp, buf);
}

void api_printer_bed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[256];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_bed(&status, buf,
                                                    sizeof(buf)),
        buf, "{\"message\":\"Printer bed response too large\"}");
}

void api_printer_bed_temp_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    deneb_printer_status_response_t status;
    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_temperature(
            status.bed_temp_cur, status.bed_temp_set, buf, sizeof(buf)),
        buf, "{\"message\":\"Printer temperature response too large\"}");
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
    (void)req;
    char buf[640];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_heads(&status, buf,
                                                      sizeof(buf)),
        buf, "{\"message\":\"Printer heads response too large\"}");
}

void api_printer_head_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[512];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_head(&status, buf,
                                                     sizeof(buf)),
        buf, "{\"message\":\"Printer head response too large\"}");
}

void api_printer_position_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_position(&status, buf,
                                                         sizeof(buf)),
        buf, "{\"message\":\"Printer position response too large\"}");
}

void api_printer_extruders_get(const http_request_t *req, http_response_t *resp)
{
    /* Return extruders as an array with single element */
    (void)req;
    char buf[640];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_extruders(&status, buf,
                                                          sizeof(buf)),
        buf, "{\"message\":\"Printer extruders response too large\"}");
}

void api_printer_extruder_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[512];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_extruder(&status, buf,
                                                         sizeof(buf)),
        buf, "{\"message\":\"Printer extruder response too large\"}");
}

void api_printer_hotend_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[384];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_hotend(&status, 1, buf,
                                                       sizeof(buf)),
        buf, "{\"message\":\"Printer hotend response too large\"}");
}

void api_printer_hotend_temp_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    deneb_printer_status_response_t status;
    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_temperature(
            status.nozzle_temp_cur, status.nozzle_temp_set, buf, sizeof(buf)),
        buf, "{\"message\":\"Printer temperature response too large\"}");
}

void api_printer_feeder_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_feeder(buf, sizeof(buf)),
        buf, "{\"message\":\"Printer feeder response too large\"}");
}

void api_printer_material_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[80];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_material(buf, sizeof(buf)),
        buf, "{\"message\":\"Printer material response too large\"}");
}

void api_printer_led_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[128];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_led(buf, sizeof(buf)),
        buf, "{\"message\":\"Printer LED response too large\"}");
}

void api_printer_led_brightness_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[8];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_led_brightness(buf,
                                                               sizeof(buf)),
        buf, "{\"message\":\"Printer LED brightness response too large\"}");
}

void api_printer_led_hue_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[8];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_led_hue(buf, sizeof(buf)),
        buf, "{\"message\":\"Printer LED hue response too large\"}");
}

void api_printer_led_saturation_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[8];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_led_saturation(buf,
                                                               sizeof(buf)),
        buf, "{\"message\":\"Printer LED saturation response too large\"}");
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
    char buf[32];
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_ambient(buf, sizeof(buf)),
        buf, "{\"message\":\"Ambient response too large\"}");
}

void api_printer_airmanager_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[256];
    deneb_printer_status_response_t status;

    backend_zmq_get_printer_status_response(&status);
    set_printer_response(
        resp,
        deneb_printer_status_response_format_um_airmanager(&status, buf,
                                                           sizeof(buf)),
        buf, "{\"message\":\"Air Manager response too large\"}");
}

/* ========== M7 Write Endpoints ========== */

void api_printer_bed_temp_put(const http_request_t *req, http_response_t *resp)
{
    float temp = 0;
    char cmd[32];
    if (deneb_gcode_plan_temperature_target_from_json(
            DENEB_GCODE_HEATER_BED, req->body, &temp, cmd, sizeof(cmd)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid temperature\"}");
        return;
    }
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
    char cmd[32];
    if (deneb_gcode_plan_temperature_target_from_json(
            DENEB_GCODE_HEATER_NOZZLE, req->body, &temp, cmd, sizeof(cmd)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Invalid temperature\"}");
        return;
    }
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
    deneb_gcode_motion_plan_t plan;
    int rc;

    if (!backend_zmq_manual_action_allowed()) {
        set_motion_blocked(resp);
        return;
    }

    rc = deneb_gcode_plan_motion_from_json(req->body, &plan);
    if (rc != DENEB_GCODE_MOTION_PLAN_OK) {
        set_motion_plan_error(resp, rc);
        return;
    }

    if (send_motion_plan(&plan) < 0) {
        set_motion_failed(resp);
        return;
    }
    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_printer_position_post(const http_request_t *req, http_response_t *resp)
{
    deneb_manual_motion_plan_t plan;
    int rc;

    if (!backend_zmq_manual_action_allowed()) {
        set_motion_blocked(resp);
        return;
    }

    rc = deneb_manual_motion_plan_request(req->body, &plan);
    if (rc == DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"action\\\":\\\"home|z_home|bed_up|bed_down\\\"}\"}");
        return;
    }
    if (rc == DENEB_MANUAL_MOTION_PLAN_UNKNOWN_ACTION) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Unknown motion action\"}");
        return;
    }

    if (plan.kind == DENEB_MANUAL_MOTION_GCODE)
        rc = backend_zmq_send_gcode(plan.command);
    else if (plan.kind == DENEB_MANUAL_MOTION_MACRO)
        rc = backend_zmq_send_macro(plan.command);
    else
        rc = -1;

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
