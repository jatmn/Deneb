/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Cura local cluster API compatibility handlers.
 */

#include "api_cluster.h"
#include "api_cluster_materials.h"
#include "api_print_job.h"
#include "backend_zmq.h"
#include "command_format.h"
#include "json_writer.h"
#include "material_catalog.h"
#include "pending_job_file.h"
#include "printer_identity.h"
#include "print_profile.h"
#include "print_state_rules.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int persist_uploaded_material(const http_request_t *req);
static void write_cluster_materials_response(http_response_t *resp);

static const char *cluster_printer_status(const printer_state_t *s)
{
    return deneb_print_status_label(s->connected, s->has_error,
                                    s->is_paused, s->is_printing);
}

static const char *cluster_job_status(const printer_state_t *s)
{
    return deneb_print_job_status_label(s->has_error, s->is_paused,
                                        s->is_printing);
}

static int has_active_job(const printer_state_t *s)
{
    return deneb_print_job_is_active(s->has_error, s->is_paused,
                                     s->is_printing);
}

static int serve_pending_cluster_job(http_response_t *resp)
{
    char buf[8192];
    size_t n = 0;

    if (deneb_pending_job_file_read_raw_array(DENEB_PENDING_JOB_PATH,
                                              buf, sizeof(buf), &n) < 0)
        return 0;
    api_http_set_body(resp, buf, n);
    return 1;
}

static int load_pending_job(deneb_pending_job_file_t *job)
{
    return deneb_pending_job_file_load_default(job) == 0 &&
           job->tracker >= 0 ? 0 : -1;
}

static int pending_job_tracker(void)
{
    deneb_pending_job_file_t job;
    return load_pending_job(&job) == 0 ? job.tracker : -1;
}

static int send_pending_job_instruction(const char *instruction)
{
    deneb_pending_job_file_t job;
    if (load_pending_job(&job) < 0)
        return -1;

    if (strcmp(instruction, "PREPARE") == 0 && !job.path[0]) {
        fprintf(stderr, "deneb-api: failed to read pending job path for PREPARE\n");
        return -1;
    }
    if (strcmp(instruction, "PREPARE") == 0) {
        fprintf(stderr, "deneb-api: pending print path resolved to %s\n",
                job.path[0] ? job.path : "(none)");
    }

    fprintf(stderr, "deneb-api: sending pending job instruction=%s tracker=%d\n", instruction, job.tracker);

    if (strcmp(instruction, DENEB_COMMAND_VERB_ABORT) == 0)
        return backend_zmq_abort();

    if (strcmp(instruction, "PREPARE") == 0) {
        return backend_zmq_send_job(job.path, DENEB_PRINT_DEFAULT_JOB_SOURCE,
                                    DENEB_PRINT_DEFAULT_JOB_UUID, 0.0f, 0.0f);
    }

    return -1;
}

static void write_configuration(json_writer_t *w)
{
    char nozzle_id[24];
    char material_guid[48];
    deneb_print_profile_read_loaded_nozzle_id(nozzle_id, sizeof(nozzle_id));
    deneb_print_profile_read_loaded_material_guid(material_guid, sizeof(material_guid));

    json_key(w, "configuration");
    json_arr_open(w);
    json_obj_open(w);
    json_int(w, "extruder_index", 0);
    json_str(w, "print_core_id", nozzle_id);
    json_key(w, "material");
    json_obj_open(w);
    json_str(w, "guid", material_guid);
    json_str(w, "brand", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND);
    json_str(w, "material", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE);
    json_str(w, "color", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_COLOR);
    json_obj_close(w);
    json_obj_close(w);
    json_arr_close(w);
}

void api_cluster_materials_get(const http_request_t *req, http_response_t *resp)
{
    if (strcmp(req->method, "POST") == 0) {
        if (persist_uploaded_material(req) < 0) {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Material not added\"}");
            return;
        }
        api_http_set_body_str(resp, "{\"message\":\"Material accepted\"}");
        return;
    }
    write_cluster_materials_response(resp);
}

static int persist_uploaded_material(const http_request_t *req)
{
    char material_path[256] = "";
    char filename[128] = "material.xml";
    char guid[64];
    int version = 0;

    if (!req->multipart_boundary[0] || !req->upload_path[0]) {
        fprintf(stderr, "deneb-api: material upload rejected: missing multipart upload\n");
        return -1;
    }

    if (extract_multipart_file(req->multipart_boundary, req->upload_path,
                               material_path, sizeof(material_path),
                               filename, sizeof(filename)) < 0) {
        fprintf(stderr, "deneb-api: material upload rejected: failed to extract multipart file\n");
        return -1;
    }

    if (deneb_material_catalog_store_file(material_path,
                                          DENEB_MATERIAL_CATALOG_DIR,
                                          guid, sizeof(guid), &version) < 0) {
        fprintf(stderr, "deneb-api: material upload rejected: failed to store %s: %s\n",
                filename, strerror(errno));
        unlink(material_path);
        return -1;
    }
    unlink(material_path);
    fprintf(stderr, "deneb-api: accepted material %s version %d\n", guid, version);
    return 0;
}

static void write_cluster_materials_response(http_response_t *resp)
{
    char *body = NULL;
    size_t pos = 0;

    if (deneb_material_catalog_build_response(DENEB_CLUSTER_MATERIALS_JSON,
                                              DENEB_MATERIAL_CATALOG_DIR,
                                              &body, &pos) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Material catalog response too large\"}");
        return;
    }
    api_http_set_body(resp, body, pos);
    free(body);
}

void api_cluster_printers_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char hostname[64];
    char guid[48];
    char buf[2048];
    json_writer_t w;

    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    deneb_printer_identity_guid(guid, sizeof(guid));

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_bool(&w, "enabled", 1);
    json_str(&w, "friendly_name", hostname);
    json_str(&w, "machine_variant", DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    json_str(&w, "status", cluster_printer_status(s));
    json_str(&w, "unique_name", hostname);
    json_str(&w, "uuid", guid);
    write_configuration(&w);
    json_str(&w, "firmware_version", DENEB_VERSION);
    json_str(&w, "ip_address", "");
    json_str(&w, "reserved_by", "");
    json_bool(&w, "maintenance_required", 0);
    json_str(&w, "firmware_update_status", "up_to_date");
    json_str(&w, "latest_available_firmware", DENEB_VERSION);
    json_key(&w, "build_plate");
    json_obj_open(&w);
    json_str(&w, "type", "glass");
    json_obj_close(&w);
    json_obj_close(&w);
    json_arr_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_cluster_print_jobs_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char guid[48];
    char created_at[32];
    char buf[2048];
    json_writer_t w;

    if (!has_active_job(s)) {
        if (serve_pending_cluster_job(resp))
            return;
        api_http_set_body_str(resp, "[]");
        return;
    }

    deneb_printer_identity_guid(guid, sizeof(guid));
    snprintf(created_at, sizeof(created_at), "%lld", (long long)time(NULL));

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_str(&w, "created_at", created_at);
    json_bool(&w, "force", 0);
    json_str(&w, "machine_variant", DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    json_str(&w, "name", deneb_print_job_name_or_default(s->filename));
    json_bool(&w, "started", s->is_printing || s->is_paused);
    json_str(&w, "status", cluster_job_status(s));
    json_int(&w, "time_total", s->time_total);
    json_int(&w, "time_elapsed",
             deneb_print_elapsed_seconds(s->time_total, s->time_left));
    json_str(&w, "uuid", deneb_print_job_uuid_or_default(s->uuid));
    write_configuration(&w);
    json_str(&w, "owner", deneb_print_job_source_or_default(s->source));
    json_str(&w, "printer_uuid", guid);
    json_str(&w, "assigned_to", guid);
    json_key(&w, "build_plate");
    json_obj_open(&w);
    json_str(&w, "type", "glass");
    json_obj_close(&w);
    json_key(&w, "compatible_machine_families");
    json_arr_open(&w);
    json_arr_str(&w, DENEB_PRINT_PROFILE_MACHINE_FAMILY);
    json_arr_str(&w, DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    json_arr_close(&w);
    json_key(&w, "impediments_to_printing");
    json_arr_open(&w);
    json_arr_close(&w);
    json_obj_close(&w);
    json_arr_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_cluster_print_jobs_post(const http_request_t *req, http_response_t *resp)
{
    if (strstr(req->path, "/action/move")) {
        api_cluster_print_job_move_post(req, resp);
        return;
    }
    api_print_job_post(req, resp);
}

static void log_cluster_action(const char *action, int has_pending_job, const char *path)
{
    fprintf(stderr, "deneb-api: cluster print action=%s path=%s has_pending=%s override=%s\n",
            action ? action : "(none)",
            path ? path : "(none)",
            has_pending_job ? "true" : "false",
            deneb_print_action_is_force(action) ? "true" : "false");
}

void api_cluster_print_job_action_put(const http_request_t *req, http_response_t *resp)
{
    char action[16];
    int has_pending_job = pending_job_tracker() >= 0;
    if (!strstr(req->path, "/action")) {
        api_cluster_print_job_put(req, resp);
        return;
    }

    if (deneb_print_action_parse(req->body, action, sizeof(action)) < 0) {
        if (has_pending_job) {
            snprintf(action, sizeof(action), "print");
        } else {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"action\\\":\\\"pause|print|abort\\\"}\"}");
            return;
        }
    }

    log_cluster_action(action, has_pending_job, req->path);

    if (deneb_print_action_is_resume_or_start(action) && has_pending_job) {
        if (send_pending_job_instruction("PREPARE") != 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to continue print\"}");
            return;
        }
        /* Keep pending print metadata visible during preheat / queued startup. */
    } else if (deneb_print_action_is_abort(action) && has_pending_job) {
        if (send_pending_job_instruction(DENEB_COMMAND_VERB_ABORT) != 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to cancel print\"}");
            return;
        }
        deneb_pending_job_file_clear_default();
    } else if (deneb_print_action_is_pause(action)) {
        if (backend_zmq_pause() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to pause print\"}");
            return;
        }
    } else if (deneb_print_action_is_resume_or_start(action)) {
        if (backend_zmq_resume() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to resume print\"}");
            return;
        }
    } else if (deneb_print_action_is_abort(action)) {
        if (backend_zmq_abort() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
            return;
        }
        deneb_pending_job_file_clear_default();
    } else if (deneb_print_action_is_stop(action)) {
        if (backend_zmq_stop_print() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to stop print\"}");
            return;
        }
        deneb_pending_job_file_clear_default();
    } else {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Unknown print job action\"}");
        return;
    }

    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_cluster_print_job_move_post(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status_code = 204;
}

void api_cluster_print_job_put(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
}

void api_cluster_print_job_delete(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    if (!has_active_job(s)) {
        deneb_pending_job_file_clear_default();
        resp->status_code = 204;
        return;
    }

    if (backend_zmq_abort() < 0) {
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
        return;
    }
    resp->status_code = 204;
}

void api_cluster_print_job_preview_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status_code = 404;
    resp->content_type[0] = '\0';
    api_http_set_body_str(resp, "");
}
