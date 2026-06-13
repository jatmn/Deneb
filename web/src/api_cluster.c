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
static void format_utc_time(time_t timestamp, char *dst, size_t dst_size);

static void format_utc_now(char *dst, size_t dst_size)
{
    time_t now;

    now = time(NULL);
    format_utc_time(now, dst, dst_size);
}

static void format_utc_time(time_t timestamp, char *dst, size_t dst_size)
{
    struct tm tm_now;

    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    if (!gmtime_r(&timestamp, &tm_now))
        return;
    strftime(dst, dst_size, "%Y-%m-%dT%H:%M:%S.000Z", &tm_now);
}

static void read_local_ip(char *dst, size_t dst_size)
{
    FILE *fp;

    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    fp = popen("ip -4 addr show | awk '/inet / && $2 !~ /^127/ { sub(\"/.*\", \"\", $2); print $2; exit }'",
               "r");
    if (!fp) {
        snprintf(dst, dst_size, "127.0.0.1");
        return;
    }
    if (!fgets(dst, dst_size, fp))
        dst[0] = '\0';
    pclose(fp);
    dst[strcspn(dst, "\r\n")] = '\0';
    if (dst[0] == '\0')
        snprintf(dst, dst_size, "127.0.0.1");
}

static void read_uci_line(const char *option,
                          const char *fallback,
                          char *dst,
                          size_t dst_size)
{
    FILE *fp;
    char cmd[128];

    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    snprintf(cmd, sizeof(cmd), "uci -q get %s 2>/dev/null", option);
    fp = popen(cmd, "r");
    if (fp) {
        (void)fgets(dst, dst_size, fp);
        pclose(fp);
    }
    dst[strcspn(dst, "\r\n")] = '\0';
    if (dst[0] == '\0')
        snprintf(dst, dst_size, "%s", fallback ? fallback : "");
}

static int json_member_value_end(const char *start, const char *limit,
                                 const char **end)
{
    const char *p = start;
    int depth = 0;
    int in_string = 0;
    int escape = 0;

    while (p < limit && *p) {
        char c = *p;
        if (in_string) {
            if (escape)
                escape = 0;
            else if (c == '\\')
                escape = 1;
            else if (c == '"')
                in_string = 0;
            p++;
            continue;
        }

        if (c == '"') {
            in_string = 1;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (depth == 0)
                break;
            depth--;
        } else if (c == ',' && depth == 0) {
            *end = p + 1;
            return 1;
        }
        p++;
    }

    *end = p;
    return 0;
}

static void json_remove_member(char *json, const char *key)
{
    char pattern[64];
    char *field;
    char *start;
    const char *value_start;
    const char *end;
    size_t remove_len;
    int consumed_comma;

    if (!json || !key || strlen(key) + 4 >= sizeof(pattern))
        return;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    while ((field = strstr(json, pattern)) != NULL) {
        value_start = field + strlen(pattern);
        consumed_comma = json_member_value_end(value_start,
                                               json + strlen(json), &end);
        start = field;
        if (!consumed_comma) {
            while (start > json && (start[-1] == ' ' || start[-1] == '\n' ||
                                   start[-1] == '\r' || start[-1] == '\t'))
                start--;
            if (start > json && start[-1] == ',')
                start--;
        }
        remove_len = (size_t)(end - start);
        memmove(start, end, strlen(end) + 1);
    }
}

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
    json_remove_member(buf, "path");
    json_remove_member(buf, "owner");
    json_remove_member(buf, "build_plate");
    json_remove_member(buf, "compatible_machine_families");
    json_remove_member(buf, "impediments_to_printing");
    json_remove_member(buf, "deneb_tracker");
    api_http_set_body(resp, buf, strlen(buf));
    return 1;
}

static void write_configuration(json_writer_t *w)
{
    char nozzle_id[24];
    char material_guid[48];
    char material_type[32];
    char material_color[32];
    deneb_print_profile_read_loaded_nozzle_id(nozzle_id, sizeof(nozzle_id));
    deneb_print_profile_read_loaded_cluster_material_guid(material_guid,
                                                          sizeof(material_guid));
    deneb_print_profile_material_type_from_guid(material_guid, material_type,
                                                sizeof(material_type));
    deneb_print_profile_material_color_from_guid(material_guid, material_color,
                                                 sizeof(material_color));

    json_key(w, "configuration");
    json_arr_open(w);
    json_obj_open(w);
    json_int(w, "extruder_index", 0);
    json_str(w, "print_core_id", nozzle_id);
    json_key(w, "material");
    json_obj_open(w);
    if (material_guid[0])
        json_str(w, "guid", material_guid);
    json_str(w, "brand", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND);
    json_str(w, "material", material_type);
    json_str(w, "color", material_color);
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
    char friendly_name[64];
    char guid[48];
    char ip_address[64];
    char current_firmware[32];
    char latest_firmware[32];
    const char *firmware_update_status = "up_to_date";
    char buf[2048];
    json_writer_t w;

    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    deneb_printer_identity_friendly_name(friendly_name, sizeof(friendly_name));
    deneb_printer_identity_guid(guid, sizeof(guid));
    read_local_ip(ip_address, sizeof(ip_address));
    read_uci_line("ultimaker.version.nr", DENEB_VERSION, current_firmware,
                  sizeof(current_firmware));
    read_uci_line("ultimaker.version.latest", " ", latest_firmware,
                  sizeof(latest_firmware));
    if (strcmp(latest_firmware, " ") != 0 &&
        strcmp(latest_firmware, current_firmware) != 0)
        firmware_update_status = "update_available";

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_bool(&w, "enabled", 1);
    json_str(&w, "friendly_name", friendly_name);
    json_str(&w, "machine_variant", DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    json_str(&w, "status",
             deneb_print_cluster_printer_status_label(
                 backend_zmq_get_status_label()));
    json_str(&w, "unique_name", hostname);
    json_str(&w, "uuid", guid);
    write_configuration(&w);
    json_str(&w, "firmware_version", current_firmware);
    json_str(&w, "ip_address", ip_address);
    json_str(&w, "firmware_update_status", firmware_update_status);
    json_str(&w, "latest_available_firmware", latest_firmware);
    json_key(&w, "build_plate");
    json_obj_open(&w);
    json_str(&w, "type", "glass");
    json_obj_close(&w);
    json_key(&w, "faults");
    json_arr_open(&w);
    json_arr_close(&w);
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
    if (summary.created_at > 0)
        format_utc_time((time_t)summary.created_at, created_at,
                        sizeof(created_at));
    else
        format_utc_now(created_at, sizeof(created_at));

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
