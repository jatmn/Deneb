/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#include "api_print_job.h"
#include "api_multipart.h"
#include "backend_zmq.h"
#include "pending_job_file.h"
#include "pending_job_registration.h"
#include "print_action_dispatch.h"
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

static int dispatch_pause(void *ctx)
{
    (void)ctx;
    return backend_zmq_pause();
}

static int dispatch_resume(void *ctx)
{
    (void)ctx;
    return backend_zmq_resume();
}

static int dispatch_abort(void *ctx)
{
    (void)ctx;
    return backend_zmq_abort();
}

static int dispatch_stop(void *ctx)
{
    (void)ctx;
    return backend_zmq_stop_print();
}

static void dispatch_clear_pending(void *ctx)
{
    (void)ctx;
    deneb_pending_job_file_clear_default();
}

static int registration_start_allowed(void *ctx)
{
    (void)ctx;
    return backend_zmq_print_start_allowed();
}

static int registration_send_job(void *ctx,
                                 const deneb_print_job_start_plan_t *plan)
{
    (void)ctx;
    if (!plan)
        return -1;
    return backend_zmq_send_job(plan->path, plan->source, plan->uuid,
                                plan->cloud_job_id, plan->bed_target,
                                plan->nozzle_target);
}

static int register_native_print(const char *path,
                                 const char *source,
                                 const char *uuid,
                                 const char *cloud_job_id)
{
    deneb_pending_job_registration_t registration;
    deneb_pending_job_registration_dispatch_ops_t ops = {
        NULL,
        registration_start_allowed,
        registration_send_job
    };

    fprintf(stderr, "deneb-api: registering print path natively: %s\n", path);

    if (deneb_pending_job_registration_prepare(
            path, source, uuid, cloud_job_id, (long long)time(NULL),
            &registration) < 0 ||
        deneb_pending_job_registration_write_default(&registration) < 0) {
        fprintf(stderr, "deneb-api: failed to write pending print metadata for %s\n", path);
        return -1;
    }

    if (!registration.should_start_immediately) {
        fprintf(stderr, "deneb-api: pending print waits for user action changes=%d\n",
                registration.change_count);
        return 0;
    }

    if (deneb_pending_job_registration_dispatch_start(
            &registration, &ops) < 0) {
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
    if (deneb_print_job_summary_format_um_response(&summary, buf,
                                                   sizeof(buf)) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp,
                              "{\"message\":\"Print job response too large\"}");
        return;
    }
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
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_STATE, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_progress_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_progress_fraction(&summary, buf,
                                                     sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_time_elapsed(&summary, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_time_total(&summary, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[196];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_NAME, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[96];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_UUID, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_source_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[64];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_SOURCE, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_started_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[4];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_STARTED, buf,
        sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_finished_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[4];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_FINISHED, buf,
        sizeof(buf));
    api_http_set_body_str(resp, buf);
}

/* ========== M7 Write Endpoints ========== */

int api_print_job_dispatch_plan(const deneb_print_action_plan_t *plan,
                                http_response_t *resp)
{
    deneb_print_action_dispatch_ops_t ops = {
        NULL,
        dispatch_pause,
        dispatch_resume,
        dispatch_abort,
        dispatch_stop,
        dispatch_clear_pending
    };

    if (!plan) {
        resp->status_code = 400;
        api_http_set_body_str(resp, deneb_print_action_unknown_response());
        return -1;
    }

    if (deneb_print_action_dispatch(plan, &ops) < 0) {
        set_message_response(resp, 503, plan->failure_message);
        return -1;
    }

    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    return 0;
}

int api_print_job_dispatch_action(const char *action, http_response_t *resp,
                                  const char *unknown_message)
{
    deneb_print_action_plan_t plan;

    if (deneb_print_action_plan(action, &plan) != 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, unknown_message ?
                              unknown_message :
                              deneb_print_action_unknown_response());
        return -1;
    }

    return api_print_job_dispatch_plan(&plan, resp);
}

void api_print_job_state_put(const http_request_t *req, http_response_t *resp)
{
    /* Body is a plain string: "pause", "print", or "abort" */
    char cmd[16];
    const char *action =
        deneb_print_action_parse(req->body, cmd, sizeof(cmd)) == 0 ? cmd : NULL;
    log_print_job_state_cmd(action, req->body);

    api_print_job_dispatch_action(action, resp,
                                  deneb_print_state_unknown_response());
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
    char job_instance_uuid[96] = "";
    char cloud_job_id[96] = "";
    char owner[32] = "";
    deneb_print_job_upload_storage_plan_t storage;

    if (req->multipart_boundary[0] &&
        extract_multipart_file(req->multipart_boundary, req->upload_path,
                               gcode_path, sizeof(gcode_path),
                               filename, sizeof(filename)) < 0) {
        fprintf(stderr, "deneb-api: print upload rejected: failed to extract multipart file\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Failed to extract file from upload\"}");
        return;
    }
    if (req->multipart_boundary[0]) {
        (void)extract_multipart_field(req->multipart_boundary, req->upload_path,
                                      "job_instance_uuid", job_instance_uuid,
                                      sizeof(job_instance_uuid));
        (void)extract_multipart_field(req->multipart_boundary, req->upload_path,
                                      "cloud_job_id", cloud_job_id,
                                      sizeof(cloud_job_id));
        (void)extract_multipart_field(req->multipart_boundary, req->upload_path,
                                      "owner", owner, sizeof(owner));
    }

    if (gcode_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no file field\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"No file field in upload\"}");
        return;
    }

    if (deneb_print_job_file_upload_storage_plan(filename, &storage) < 0) {
        fprintf(stderr, "deneb-api: failed to prepare print spool path for %s: %s\n",
                filename, strerror(errno));
        unlink(gcode_path);
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Failed to prepare print storage\"}");
        return;
    }

    deneb_pending_job_upload_check_t pending_upload;
    if (deneb_pending_job_file_check_upload_default(storage.dest_path,
                                                    storage.filename,
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

    if (deneb_print_job_file_store_upload(gcode_path, storage.dest_path) < 0) {
        fprintf(stderr, "deneb-api: failed to save print file to %s: %s\n",
                storage.dest_path, strerror(errno));
        unlink(gcode_path);
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Failed to save file\"}");
        return;
    }

    fprintf(stderr, "deneb-api: print file saved to %s\n", storage.dest_path);

    fprintf(stderr, "deneb-api: registration request sent for %s (%s)\n",
            storage.filename, storage.dest_path);

    if (register_native_print(storage.dest_path,
                              owner[0] ? owner : NULL,
                              job_instance_uuid[0] ? job_instance_uuid : NULL,
                              cloud_job_id[0] ? cloud_job_id : NULL) < 0) {
        fprintf(stderr, "deneb-api: failed to register print natively for %s\n",
                storage.dest_path);
        deneb_pending_job_file_clear_default();
        unlink(storage.dest_path);
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to start print\"}");
        return;
    }
    fprintf(stderr, "deneb-api: native registration accepted and print metadata prepared for %s\n",
            storage.filename);

    char buf[512];

    deneb_print_job_summary_format_queued_response(
        "Print job accepted", storage.filename, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
