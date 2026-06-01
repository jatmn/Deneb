/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Printer endpoint handlers. /api/v1/printer/*
 */

#ifndef API_PRINTER_H
#define API_PRINTER_H

#include "api_http.h"

void api_printer_get(const http_request_t *req, http_response_t *resp);
void api_printer_status_get(const http_request_t *req, http_response_t *resp);
void api_printer_bed_get(const http_request_t *req, http_response_t *resp);
void api_printer_bed_temp_get(const http_request_t *req, http_response_t *resp);
void api_printer_bed_type_get(const http_request_t *req, http_response_t *resp);
void api_printer_bed_preheat_get(const http_request_t *req, http_response_t *resp);
void api_printer_heads_get(const http_request_t *req, http_response_t *resp);
void api_printer_head_get(const http_request_t *req, http_response_t *resp);
void api_printer_position_get(const http_request_t *req, http_response_t *resp);
void api_printer_extruders_get(const http_request_t *req, http_response_t *resp);
void api_printer_extruder_get(const http_request_t *req, http_response_t *resp);
void api_printer_hotend_get(const http_request_t *req, http_response_t *resp);
void api_printer_hotend_temp_get(const http_request_t *req, http_response_t *resp);
void api_printer_feeder_get(const http_request_t *req, http_response_t *resp);
void api_printer_material_get(const http_request_t *req, http_response_t *resp);
void api_printer_led_get(const http_request_t *req, http_response_t *resp);
void api_printer_led_brightness_get(const http_request_t *req, http_response_t *resp);
void api_printer_led_hue_get(const http_request_t *req, http_response_t *resp);
void api_printer_led_saturation_get(const http_request_t *req, http_response_t *resp);
void api_printer_network_get(const http_request_t *req, http_response_t *resp);
void api_printer_ambient_get(const http_request_t *req, http_response_t *resp);
void api_printer_airmanager_get(const http_request_t *req, http_response_t *resp);

/* M7 write endpoints */
void api_printer_bed_temp_put(const http_request_t *req, http_response_t *resp);
void api_printer_hotend_temp_put(const http_request_t *req, http_response_t *resp);
void api_printer_bed_preheat_put(const http_request_t *req, http_response_t *resp);
void api_printer_position_put(const http_request_t *req, http_response_t *resp);
void api_printer_position_post(const http_request_t *req, http_response_t *resp);
void api_printer_led_put(const http_request_t *req, http_response_t *resp);
void api_printer_led_brightness_put(const http_request_t *req, http_response_t *resp);
void api_printer_validate_post(const http_request_t *req, http_response_t *resp);

#endif
