/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint handlers. /api/v1/print_job/*
 */

#ifndef API_PRINT_JOB_H
#define API_PRINT_JOB_H

#include "api_http.h"
#include "print_job_file.h"
#include "print_state_rules.h"

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

typedef struct {
    deneb_print_job_upload_storage_plan_t storage;
    char staged_directory[sizeof(DENEB_PRINT_JOB_SPOOL_DIR "/.upload-XXXXXX")];
    char staged_gcode[sizeof(DENEB_PRINT_JOB_SPOOL_DIR "/.upload-XXXXXX/job")];
    char staged_thumbnail[128];
    char job_instance_uuid[96];
    char cloud_job_id[96];
    char owner[32];
    int ufp_normalized;
} api_print_upload_t;

int api_print_job_prepare_upload(const http_request_t *req,
                                 api_print_upload_t *upload,
                                 http_response_t *resp,
                                 const atomic_bool *cancelled);
void api_print_job_commit_upload(const api_print_upload_t *upload,
                                  http_response_t *resp);
void api_print_job_release_upload(const api_print_upload_t *upload);
int api_print_job_dispatch_action(const char *action, http_response_t *resp,
                                  const char *unknown_message);
int api_print_job_dispatch_plan(const deneb_print_action_plan_t *plan,
                                http_response_t *resp);

#endif
