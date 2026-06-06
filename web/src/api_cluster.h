/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Cura local cluster API compatibility handlers.
 */

#ifndef API_CLUSTER_H
#define API_CLUSTER_H

#include "api_http.h"

void api_cluster_materials_get(const http_request_t *req, http_response_t *resp);
void api_cluster_printers_get(const http_request_t *req, http_response_t *resp);
void api_cluster_print_jobs_get(const http_request_t *req, http_response_t *resp);
void api_cluster_print_jobs_post(const http_request_t *req, http_response_t *resp);
void api_cluster_print_job_action_put(const http_request_t *req, http_response_t *resp);
void api_cluster_print_job_move_post(const http_request_t *req, http_response_t *resp);
void api_cluster_print_job_put(const http_request_t *req, http_response_t *resp);
void api_cluster_print_job_delete(const http_request_t *req, http_response_t *resp);
void api_cluster_print_job_preview_get(const http_request_t *req, http_response_t *resp);

#endif /* API_CLUSTER_H */
