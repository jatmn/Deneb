/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * System endpoint handlers. /api/v1/system/*
 */

#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include "api_http.h"

void api_system_get(const http_request_t *req, http_response_t *resp);
void api_system_name_get(const http_request_t *req, http_response_t *resp);
void api_system_hostname_get(const http_request_t *req, http_response_t *resp);
void api_system_firmware_get(const http_request_t *req, http_response_t *resp);
void api_system_hardware_get(const http_request_t *req, http_response_t *resp);
void api_system_type_get(const http_request_t *req, http_response_t *resp);
void api_system_variant_get(const http_request_t *req, http_response_t *resp);
void api_system_platform_get(const http_request_t *req, http_response_t *resp);
void api_system_memory_get(const http_request_t *req, http_response_t *resp);
void api_system_uptime_get(const http_request_t *req, http_response_t *resp);
void api_system_language_get(const http_request_t *req, http_response_t *resp);
void api_system_country_get(const http_request_t *req, http_response_t *resp);
void api_system_time_get(const http_request_t *req, http_response_t *resp);
void api_system_guid_get(const http_request_t *req, http_response_t *resp);
void api_system_log_get(const http_request_t *req, http_response_t *resp);

#endif
