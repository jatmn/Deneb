/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint handlers. /api/v1/print_job/*
 */

#ifndef API_PRINT_JOB_H
#define API_PRINT_JOB_H

#include "api_http.h"

void api_print_job_get(const http_request_t *req, http_response_t *resp);
void api_print_job_state_get(const http_request_t *req, http_response_t *resp);
void api_print_job_progress_get(const http_request_t *req, http_response_t *resp);
void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp);
void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp);
void api_print_job_name_get(const http_request_t *req, http_response_t *resp);
void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp);
void api_print_job_source_get(const http_request_t *req, http_response_t *resp);
void api_print_job_datetime_started_get(const http_request_t *req, http_response_t *resp);
void api_print_job_datetime_finished_get(const http_request_t *req, http_response_t *resp);

/* M7 write endpoints */
void api_print_job_state_put(const http_request_t *req, http_response_t *resp);
void api_print_job_post(const http_request_t *req, http_response_t *resp);

#endif
