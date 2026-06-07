/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"
#include "pending_job.h"
#include "pending_job_file.h"
#include "print_job_file.h"
#include "print_profile.h"
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

static void write_pending_job_response(http_response_t *resp, const char *job_name, int status_code)
{
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "message", "Print job already queued");
    json_str(&w, "name", job_name[0] ? job_name : "Print job");
    json_str(&w, "uuid", DENEB_PRINT_STOCK_API_JOB_UUID);
    json_str(&w, "source", DENEB_PRINT_WEB_API_JOB_SOURCE);
    json_str(&w, "state", DENEB_PRINT_PHASE_NAME_PRE_PRINT);
    json_float(&w, "progress", 0.0);
    json_int(&w, "time_elapsed", 0);
    json_int(&w, "time_total", 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
    resp->status_code = status_code;
}

static int send_native_job_start(const char *path)
{
    return backend_zmq_send_job(path, DENEB_PRINT_DEFAULT_JOB_SOURCE,
                                DENEB_PRINT_DEFAULT_JOB_UUID, 0.0f, 0.0f);
}

static int register_native_print(const char *path)
{
    deneb_pending_job_t job;
    char loaded_guid[64];
    char loaded_nozzle[24];
    char target_guid[64];
    char target_nozzle_raw[24];
    char target_nozzle[24];
    char loaded_name[64];
    char target_name[64];
    deneb_print_job_file_metadata_t meta;

    fprintf(stderr, "deneb-api: registering print path natively: %s\n", path);

    deneb_print_profile_read_loaded_material_guid(loaded_guid, sizeof(loaded_guid));
    deneb_print_profile_read_loaded_nozzle_size(loaded_nozzle, sizeof(loaded_nozzle));

    snprintf(target_guid, sizeof(target_guid), "%s", loaded_guid);
    snprintf(target_nozzle_raw, sizeof(target_nozzle_raw), "%s", loaded_nozzle);
    deneb_print_job_file_metadata_init(&meta);
    if (deneb_print_job_file_metadata_load(path, &meta) == 0) {
        if (meta.material_guid[0])
            snprintf(target_guid, sizeof(target_guid), "%s", meta.material_guid);
        if (meta.nozzle_size[0])
            snprintf(target_nozzle_raw, sizeof(target_nozzle_raw), "%s", meta.nozzle_size);
    }
    deneb_print_profile_normalize_nozzle_id(loaded_nozzle, loaded_nozzle, sizeof(loaded_nozzle));
    deneb_print_profile_normalize_nozzle_id(target_nozzle_raw, target_nozzle, sizeof(target_nozzle));
    deneb_print_profile_material_name_from_guid(loaded_guid, loaded_name, sizeof(loaded_name));
    deneb_print_profile_material_name_from_guid(target_guid, target_name, sizeof(target_name));

    deneb_pending_job_init(&job, path);
    job.tracker = (int)(time(NULL) & 0x7fffffff);
    snprintf(job.source, sizeof(job.source), "%s",
             DENEB_PRINT_DEFAULT_JOB_SOURCE);
    snprintf(job.material_guid, sizeof(job.material_guid), "%s", target_guid);
    snprintf(job.origin_material_guid, sizeof(job.origin_material_guid), "%s", loaded_guid);
    snprintf(job.origin_material_name, sizeof(job.origin_material_name), "%s", loaded_name);
    snprintf(job.target_material_name, sizeof(job.target_material_name), "%s", target_name);
    snprintf(job.material_type, sizeof(job.material_type), "%s", target_name);
    snprintf(job.nozzle_id, sizeof(job.nozzle_id), "%s", target_nozzle);
    snprintf(job.origin_nozzle_id, sizeof(job.origin_nozzle_id), "%s", loaded_nozzle);
    job.material_change_required =
        loaded_guid[0] && target_guid[0] && strcmp(loaded_guid, target_guid) != 0;
    job.print_core_change_required =
        loaded_nozzle[0] && target_nozzle[0] && strcmp(loaded_nozzle, target_nozzle) != 0;

    if (deneb_pending_job_write_default(&job) < 0) {
        fprintf(stderr, "deneb-api: failed to write pending print metadata for %s\n", path);
        return -1;
    }

    if (deneb_pending_job_change_count(&job) > 0) {
        fprintf(stderr, "deneb-api: pending print waits for user action changes=%d\n",
                deneb_pending_job_change_count(&job));
        return 0;
    }

    if (send_native_job_start(path) < 0) {
        fprintf(stderr, "deneb-api: failed to send native JOB for %s\n", path);
        return -1;
    }

    fprintf(stderr, "deneb-api: native print registration accepted for %s\n", path);
    return 0;
}

static int is_printing(const printer_state_t *s)
{
    return deneb_print_job_is_active(s->has_error, s->is_paused,
                                     s->is_printing);
}

static const char *get_job_state(const printer_state_t *s)
{
    return deneb_print_job_state_or_none(s->has_error, s->is_paused,
                                         s->is_printing);
}

void api_print_job_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();

    if (!is_printing(s)) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }

    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "name", deneb_print_job_name_or_default(s->filename));
    json_str(&w, "uuid", deneb_print_job_uuid_or_default(s->uuid));
    json_str(&w, "source", deneb_print_job_source_or_default(s->source));
    json_str(&w, "state", get_job_state(s));
    json_float(&w, "progress", deneb_print_progress_fraction(s->progress));
    json_int(&w, "time_elapsed",
             deneb_print_elapsed_seconds(s->time_total, s->time_left));
    json_int(&w, "time_total", s->time_total);
    json_str(&w, "datetime_started", "");
    json_str(&w, "datetime_finished", "");
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_state_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    if (!is_printing(s)) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "\"%s\"", get_job_state(s));
    api_http_set_body_str(resp, buf);
}

void api_print_job_progress_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", deneb_print_progress_fraction(s->progress));
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    int elapsed = deneb_print_elapsed_seconds(s->time_total, s->time_left);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", elapsed);
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s->time_total);
    api_http_set_body_str(resp, buf);
}

void api_print_job_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[196];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, deneb_print_job_name_or_default(s->filename));
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[96];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, deneb_print_job_uuid_or_default(s->uuid));
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_source_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[64];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, deneb_print_job_source_or_default(s->source));
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
    if (deneb_print_action_is_pause(action)) {
        if (backend_zmq_pause() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to pause print\"}");
            return -1;
        }
    } else if (deneb_print_action_is_resume_or_start(action)) {
        if (backend_zmq_resume() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to resume print\"}");
            return -1;
        }
    } else if (deneb_print_action_is_abort(action)) {
        if (backend_zmq_abort() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
            return -1;
        }
        deneb_pending_job_file_clear_default();
    } else if (deneb_print_action_is_stop(action)) {
        if (backend_zmq_stop_print() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to stop print\"}");
            return -1;
        }
        deneb_pending_job_file_clear_default();
    } else {
        resp->status_code = 400;
        api_http_set_body_str(resp, unknown_message ?
                              unknown_message :
                              "{\"message\":\"Unknown print job action\"}");
        return -1;
    }

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

    deneb_pending_job_file_t pending;
    if (deneb_pending_job_file_load_default(&pending) == 0 && pending.path[0]) {
        if (deneb_pending_job_file_same_path(pending.path, dest_path)) {
            const char *pending_name = pending.name[0] ? pending.name : filename;

            fprintf(stderr, "deneb-api: print upload deduped to existing pending job path=%s\n", pending.path);
            write_pending_job_response(resp, pending_name, 200);
            unlink(gcode_path);
            return;
        }

        fprintf(stderr,
                "deneb-api: print upload rejected because another pending print exists: %s\n",
                pending.path);
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

    /* Return print job info */
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "message", "Print job accepted");
    json_str(&w, "name", filename);
    json_str(&w, "uuid", DENEB_PRINT_STOCK_API_JOB_UUID);
    json_str(&w, "source", DENEB_PRINT_WEB_API_JOB_SOURCE);
    json_str(&w, "state", DENEB_PRINT_PHASE_NAME_PRE_PRINT);
    json_float(&w, "progress", 0.0);
    json_int(&w, "time_elapsed", 0);
    json_int(&w, "time_total", 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
