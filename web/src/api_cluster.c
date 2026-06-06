/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Cura local cluster API compatibility handlers.
 */

#include "api_cluster.h"
#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define DENEB_CURA_MACHINE_VARIANT "deneb_ultimaker2_plus_connect"
#define DENEB_CLUSTER_JOB_UUID "deneb-current-job"

static void read_line_command(const char *cmd, char *out, size_t out_sz, const char *fallback)
{
    FILE *f = popen(cmd, "r");
    if (f) {
        if (fgets(out, out_sz, f) && out[0]) {
            char *nl = strchr(out, '\n');
            if (nl) *nl = '\0';
            pclose(f);
            return;
        }
        pclose(f);
    }
    snprintf(out, out_sz, "%s", fallback);
}

static void read_hostname(char *out, size_t out_sz)
{
    read_line_command("cat /proc/sys/kernel/hostname 2>/dev/null", out, out_sz, "deneb");
}

static void read_guid(char *out, size_t out_sz)
{
    read_line_command("uci -q get deneb.system.guid 2>/dev/null", out, out_sz,
        "00000000-0000-0000-0000-000000000000");
}

static const char *cluster_printer_status(const printer_state_t *s)
{
    if (s->has_error) return "error";
    if (s->is_paused) return "paused";
    if (s->is_printing) return "printing";
    if (!s->connected) return "offline";
    return "idle";
}

static const char *cluster_job_status(const printer_state_t *s)
{
    if (s->has_error) return "error";
    if (s->is_paused) return "paused";
    if (s->is_printing) return "printing";
    return "finished";
}

static int has_active_job(const printer_state_t *s)
{
    return s->is_printing || s->is_paused || s->has_error;
}

static int elapsed_seconds(const printer_state_t *s)
{
    if (s->time_total <= 0) return 0;
    if (s->time_left <= 0) return s->time_total;
    if (s->time_left >= s->time_total) return 0;
    return s->time_total - s->time_left;
}

static const char *active_job_uuid(const printer_state_t *s)
{
    return s->uuid[0] ? s->uuid : DENEB_CLUSTER_JOB_UUID;
}

static const char *active_job_name(const printer_state_t *s)
{
    return s->filename[0] ? s->filename : "Current print";
}

static void write_configuration(json_writer_t *w)
{
    json_key(w, "configuration");
    json_arr_open(w);
    json_obj_open(w);
    json_int(w, "extruder_index", 0);
    json_str(w, "print_core_id", "AA+ 0.4");
    json_key(w, "material");
    json_obj_open(w);
    json_str(w, "guid", "");
    json_str(w, "brand", "");
    json_str(w, "material", "unknown");
    json_str(w, "color", "#ffc924");
    json_obj_close(w);
    json_obj_close(w);
    json_arr_close(w);
}

void api_cluster_materials_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "[]");
}

void api_cluster_printers_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char hostname[64];
    char guid[48];
    char buf[2048];
    json_writer_t w;

    read_hostname(hostname, sizeof(hostname));
    read_guid(guid, sizeof(guid));

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_bool(&w, "enabled", 1);
    json_str(&w, "friendly_name", hostname);
    json_str(&w, "machine_variant", DENEB_CURA_MACHINE_VARIANT);
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
        api_http_set_body_str(resp, "[]");
        return;
    }

    read_guid(guid, sizeof(guid));
    snprintf(created_at, sizeof(created_at), "%lld", (long long)time(NULL));

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_str(&w, "created_at", created_at);
    json_bool(&w, "force", 0);
    json_str(&w, "machine_variant", DENEB_CURA_MACHINE_VARIANT);
    json_str(&w, "name", active_job_name(s));
    json_bool(&w, "started", s->is_printing || s->is_paused);
    json_str(&w, "status", cluster_job_status(s));
    json_int(&w, "time_total", s->time_total);
    json_int(&w, "time_elapsed", elapsed_seconds(s));
    json_str(&w, "uuid", active_job_uuid(s));
    write_configuration(&w);
    json_str(&w, "owner", s->source[0] ? s->source : "Cura");
    json_str(&w, "printer_uuid", guid);
    json_str(&w, "assigned_to", guid);
    json_key(&w, "build_plate");
    json_obj_open(&w);
    json_str(&w, "type", "glass");
    json_obj_close(&w);
    json_key(&w, "compatible_machine_families");
    json_arr_open(&w);
    json_arr_str(&w, "ultimaker2_plus_connect");
    json_arr_str(&w, DENEB_CURA_MACHINE_VARIANT);
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

static int parse_action(const char *body, char *out, size_t out_sz)
{
    const char *p = strstr(body, "\"action\"");
    if (!p) return -1;
    p = strchr(p + 8, ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        out[i++] = *p++;
    }
    if (*p != '"') return -1;
    out[i] = '\0';
    return 0;
}

void api_cluster_print_job_action_put(const http_request_t *req, http_response_t *resp)
{
    char action[16];
    if (!strstr(req->path, "/action")) {
        api_cluster_print_job_put(req, resp);
        return;
    }

    if (parse_action(req->body, action, sizeof(action)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"action\\\":\\\"pause|print|abort\\\"}\"}");
        return;
    }

    if (strcmp(action, "pause") == 0) {
        if (backend_zmq_pause() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to pause print\"}");
            return;
        }
    } else if (strcmp(action, "print") == 0 || strcmp(action, "resume") == 0) {
        if (backend_zmq_resume() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to resume print\"}");
            return;
        }
    } else if (strcmp(action, "abort") == 0) {
        if (backend_zmq_abort() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
            return;
        }
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
