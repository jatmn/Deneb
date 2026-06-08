/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"
#include "pending_job_file.h"
#include "pending_job_registration.h"
#include "print_job_file.h"
#include "print_job_summary.h"
#include "print_state_rules.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void log_print_job_state_cmd(const char *cmd, const char *body)
{
    fprintf(stderr, "deneb-api: print_job/state command=%s body=%s\n",
            cmd ? cmd : "(none)", body ? body : "{}");
}

static void set_message_response(http_response_t *resp, int status_code,
                                 const char *message)
{
    char buf[192];

    resp->status_code = status_code;
    snprintf(buf, sizeof(buf), "{\"message\":\"%s\"}",
             message ? message : "");
    api_http_set_body_str(resp, buf);
}

static void write_pending_job_response(http_response_t *resp, const char *job_name, int status_code)
{
    char buf[512];

    deneb_print_job_summary_format_queued_response(
        "Print job already queued", job_name, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
    resp->status_code = status_code;
}

static int send_native_job_start(const char *path)
{
    deneb_print_job_start_plan_t plan;

    if (deneb_print_job_start_plan_file(path, DENEB_PRINT_DEFAULT_JOB_SOURCE,
                                        &plan) < 0)
        return -1;

    return backend_zmq_send_job(plan.path, plan.source, plan.uuid,
                                plan.bed_target, plan.nozzle_target);
}

static int register_native_print(const char *path)
{
    deneb_pending_job_registration_t registration;

    fprintf(stderr, "deneb-api: registering print path natively: %s\n", path);

    if (deneb_pending_job_registration_prepare(
            path, (long long)time(NULL), &registration) < 0 ||
        deneb_pending_job_registration_write_default(&registration) < 0) {
        fprintf(stderr, "deneb-api: failed to write pending print metadata for %s\n", path);
        return -1;
    }

    if (!registration.should_start_immediately) {
        fprintf(stderr, "deneb-api: pending print waits for user action changes=%d\n",
                registration.change_count);
        return 0;
    }

    if (!backend_zmq_print_start_allowed()) {
        fprintf(stderr, "deneb-api: native print start rejected because backend is busy\n");
        return -1;
    }

    if (send_native_job_start(path) < 0) {
        fprintf(stderr, "deneb-api: failed to send native JOB for %s\n", path);
        return -1;
    }

    fprintf(stderr, "deneb-api: native print registration accepted for %s\n", path);
    return 0;
}

void api_print_job_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;

    backend_zmq_get_job_summary(&summary);
    if (!summary.active) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }

    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "name", summary.name);
    json_str(&w, "uuid", summary.uuid);
    json_str(&w, "source", summary.source);
    json_str(&w, "state", summary.state);
    json_float(&w, "progress", summary.progress_fraction);
    json_int(&w, "time_elapsed", summary.time_elapsed);
    json_int(&w, "time_total", summary.time_total);
    json_str(&w, "datetime_started", "");
    json_str(&w, "datetime_finished", "");
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_state_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    backend_zmq_get_job_summary(&summary);
    if (!summary.active) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "\"%s\"", summary.state);
    api_http_set_body_str(resp, buf);
}

void api_print_job_progress_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    snprintf(buf, sizeof(buf), "%.1f", summary.progress_fraction);
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    snprintf(buf, sizeof(buf), "%d", summary.time_elapsed);
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    snprintf(buf, sizeof(buf), "%d", summary.time_total);
    api_http_set_body_str(resp, buf);
}

void api_print_job_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[196];
    json_writer_t w;
    backend_zmq_get_job_summary(&summary);
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, summary.name);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[96];
    json_writer_t w;
    backend_zmq_get_job_summary(&summary);
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, summary.uuid);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_source_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[64];
    json_writer_t w;
    backend_zmq_get_job_summary(&summary);
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, summary.source);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_started_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"\"");
}

void api_print_job_datetime_finished_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"\"");
}

/* ========== M7 Write Endpoints ========== */

int api_print_job_dispatch_action(const char *action, http_response_t *resp,
                                  const char *unknown_message)
{
    deneb_print_action_plan_t plan;
    int rc = -1;

    if (deneb_print_action_plan(action, &plan) != 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, unknown_message ?
                              unknown_message :
                              "{\"message\":\"Unknown print job action\"}");
        return -1;
    }

    if (plan.kind == DENEB_PRINT_ACTION_PLAN_PAUSE)
        rc = backend_zmq_pause();
    else if (plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME)
        rc = backend_zmq_resume();
    else if (plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT)
        rc = backend_zmq_abort();
    else if (plan.kind == DENEB_PRINT_ACTION_PLAN_STOP)
        rc = backend_zmq_stop_print();

    if (rc < 0) {
        set_message_response(resp, 503, plan.failure_message);
        return -1;
    }

    if (plan.clear_pending_after_success)
        deneb_pending_job_file_clear_default();

    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    return 0;
}

void api_print_job_state_put(const http_request_t *req, http_response_t *resp)
{
    /* Body is a plain string: "pause", "print", or "abort" */
    char cmd[16];
    const char *action =
        deneb_print_action_parse(req->body, cmd, sizeof(cmd)) == 0 ? cmd : NULL;
    log_print_job_state_cmd(action, req->body);

    api_print_job_dispatch_action(action, resp, "{\"message\":\"Unknown state\"}");
}

void api_print_job_post(const http_request_t *req, http_response_t *resp)
{
    /*
     * POST /api/v1/print_job
     * Cura uploads a gcode file via multipart/form-data with fields:
     *   - file: the gcode/ufp file
     *   - jobname: display name (optional)
     *   - owner: who started the print (optional)
     *
     * The main.c stream_upload() has already saved the full multipart body
     * to req->upload_path. We need to extract the file content, move it
     * to internal printer storage, and send a ZMQ JOB command.
     */
    if (req->upload_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no upload_path\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"No file uploaded\"}");
        return;
    }

    /* Extract file from multipart body */
    char gcode_path[256] = "";
    char filename[128] = "upload.gcode";
    char dest_path[256];

    if (req->multipart_boundary[0] &&
        extract_multipart_file(req->multipart_boundary, req->upload_path,
                               gcode_path, sizeof(gcode_path),
                               filename, sizeof(filename)) < 0) {
        fprintf(stderr, "deneb-api: print upload rejected: failed to extract multipart file\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Failed to extract file from upload\"}");
        return;
    }

    if (gcode_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no file field\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"No file field in upload\"}");
        return;
    }

    if (deneb_print_job_file_sanitize_name(filename, filename,
                                           sizeof(filename)) < 0 ||
        deneb_print_job_file_spool_path(filename, dest_path,
                                        sizeof(dest_path)) < 0) {
        fprintf(stderr, "deneb-api: failed to prepare print spool path for %s: %s\n",
                filename, strerror(errno));
        unlink(gcode_path);
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Failed to prepare print storage\"}");
        return;
    }

    deneb_pending_job_upload_check_t pending_upload;
    if (deneb_pending_job_file_check_upload_default(dest_path, filename,
                                                    &pending_upload) == 0 &&
        pending_upload.status != DENEB_PENDING_JOB_UPLOAD_CLEAR) {
        if (pending_upload.status == DENEB_PENDING_JOB_UPLOAD_DUPLICATE) {
            fprintf(stderr, "deneb-api: print upload deduped to existing pending job path=%s\n",
                    pending_upload.path);
            write_pending_job_response(resp, pending_upload.display_name, 200);
            unlink(gcode_path);
            return;
        }

        fprintf(stderr,
                "deneb-api: print upload rejected because another pending print exists: %s\n",
                pending_upload.path);
        resp->status_code = 409;
        api_http_set_body_str(resp, "{\"message\":\"Another print job is already pending\"}");
        unlink(gcode_path);
        return;
    }

    if (deneb_print_job_file_store_upload(gcode_path, dest_path) < 0) {
        fprintf(stderr, "deneb-api: failed to save print file to %s: %s\n",
                dest_path, strerror(errno));
        unlink(gcode_path);
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Failed to save file\"}");
        return;
    }

    fprintf(stderr, "deneb-api: print file saved to %s\n", dest_path);

    fprintf(stderr, "deneb-api: registration request sent for %s (%s)\n", filename, dest_path);

    if (register_native_print(dest_path) < 0) {
        fprintf(stderr, "deneb-api: failed to register print natively for %s\n", dest_path);
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to start print\"}");
        return;
    }
    fprintf(stderr, "deneb-api: native registration accepted and print metadata prepared for %s\n", filename);

    char buf[512];

    deneb_print_job_summary_format_queued_response(
        "Print job accepted", filename, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
