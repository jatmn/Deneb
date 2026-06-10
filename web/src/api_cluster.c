/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Cura local cluster API compatibility handlers.
 */

#include "api_cluster.h"
#include "api_cluster_materials.h"
#include "api_multipart.h"
#include "api_print_job.h"
#include "backend_zmq.h"
#include "command_format.h"
#include "json_writer.h"
#include "material_catalog.h"
#include "pending_job_file.h"
#include "printer_identity.h"
#include "print_job_summary.h"
#include "print_profile.h"
#include "print_state_rules.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int persist_uploaded_material(const http_request_t *req);
static void write_cluster_materials_response(http_response_t *resp);

static void set_json_message(http_response_t *resp,
                             int status_code,
                             const char *message)
{
    char buf[160];

    resp->status_code = status_code;
    snprintf(buf, sizeof(buf), "{\"message\":\"%s\"}",
             message ? message : "");
    api_http_set_body_str(resp, buf);
}

static int serve_pending_cluster_job(http_response_t *resp)
{
    char buf[8192];
    size_t n = 0;

    if (deneb_pending_job_file_read_default_raw_array(buf, sizeof(buf), &n) < 0)
        return 0;
    api_http_set_body(resp, buf, n);
    return 1;
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

    if (deneb_material_catalog_store_uploaded_file(material_path,
                                                   guid, sizeof(guid),
                                                   &version) < 0) {
        fprintf(stderr, "deneb-api: material upload rejected: failed to store %s: %s\n",
                filename, strerror(errno));
        return -1;
    }
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
    json_str(&w, "status", backend_zmq_get_status_label());
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
    char guid[48];
    char created_at[32];
    char buf[2048];
    deneb_print_job_summary_t summary;

    backend_zmq_get_job_summary(&summary);
    if (!summary.active) {
        if (serve_pending_cluster_job(resp))
            return;
        api_http_set_body_str(resp, "[]");
        return;
    }

    deneb_printer_identity_guid(guid, sizeof(guid));
    snprintf(created_at, sizeof(created_at), "%lld", (long long)time(NULL));

    if (deneb_print_job_summary_format_cluster_active_response(
            &summary, guid, created_at, buf, sizeof(buf)) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Print job response too large\"}");
        return;
    }
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
    deneb_print_action_plan_t action_plan;
    deneb_print_action_route_t action_route;
    int pending_job = deneb_pending_job_file_has_pending_default();
    if (!strstr(req->path, "/action")) {
        api_cluster_print_job_put(req, resp);
        return;
    }

    if (deneb_print_action_parse_or_pending_default(
            req->body, pending_job, action, sizeof(action)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp,
                              deneb_print_action_parse_error_response());
        return;
    }

    log_cluster_action(action, pending_job, req->path);

    if (deneb_print_cluster_action_plan(action, pending_job, &action_plan,
                                        &action_route) != 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, deneb_print_action_unknown_response());
        return;
    }

    if (action_route == DENEB_PRINT_ACTION_ROUTE_PENDING) {
        if (backend_zmq_send_pending_instruction(action_plan.command) != 0) {
            set_json_message(resp, 503, action_plan.failure_message);
            return;
        }
    } else {
        api_print_job_dispatch_plan(&action_plan, resp);
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
    deneb_print_action_plan_t plan;

    (void)req;

    if (deneb_print_delete_action_plan(backend_zmq_has_active_job(),
                                       &plan) != 0 ||
        api_print_job_dispatch_plan(&plan, resp) != 0)
        return;

    resp->status_code = 204;
}

void api_cluster_print_job_preview_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status_code = 404;
    resp->content_type[0] = '\0';
    api_http_set_body_str(resp, "");
}
