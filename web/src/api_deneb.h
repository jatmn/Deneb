/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb extension endpoints. /api/v1/deneb/*
 */

#ifndef API_DENEB_H
#define API_DENEB_H

#include "api_http.h"

void api_deneb_events_get(const http_request_t *req, http_response_t *resp);
void api_deneb_version_get(const http_request_t *req, http_response_t *resp);
void api_deneb_locale_get(const http_request_t *req, http_response_t *resp);
void api_deneb_setup_post(const http_request_t *req, http_response_t *resp);
void api_deneb_auth_post(const http_request_t *req, http_response_t *resp);
void api_deneb_config_get(const http_request_t *req, http_response_t *resp);
void api_deneb_print_jobs_get(const http_request_t *req, http_response_t *resp);
void api_deneb_print_backend_get(const http_request_t *req, http_response_t *resp);

#endif
