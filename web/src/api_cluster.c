/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Cura local cluster API compatibility handlers.
 */

#include "api_cluster.h"
#include "api_cluster_materials.h"
#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"
#include "pending_job_file.h"
#include "print_state_rules.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DENEB_CURA_MACHINE_FAMILY "ultimaker2_plus_connect"
#define DENEB_CURA_MACHINE_VARIANT "Ultimaker 2+ Connect"
#define DENEB_CLUSTER_JOB_UUID "deneb-current-job"
#define DENEB_CLUSTER_MATERIAL_DIR "/home/3D/deneb-materials"
#define DENEB_DEFAULT_NOZZLE_SIZE "0.4"
#define DENEB_DEFAULT_MATERIAL_GUID "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"
#define DENEB_DEFAULT_MATERIAL_BRAND "Generic"
#define DENEB_DEFAULT_MATERIAL_TYPE "PLA"
#define DENEB_DEFAULT_MATERIAL_COLOR "#ffc924"

static void read_line_command(const char *cmd, char *out, size_t out_sz, const char *fallback);
static int persist_uploaded_material(const http_request_t *req);
static void write_cluster_materials_response(http_response_t *resp);

static void read_nozzle_size(char *out, size_t out_sz)
{
    read_line_command("uci -q get ultimaker.option.nozzle_size 2>/dev/null",
                      out, out_sz, DENEB_DEFAULT_NOZZLE_SIZE);
}

static void read_nozzle_id(char *out, size_t out_sz)
{
    char nozzle[16];
    read_nozzle_size(nozzle, sizeof(nozzle));
    snprintf(out, out_sz, "%s mm", nozzle);
}

static void read_material_guid(char *out, size_t out_sz)
{
    read_line_command("uci -q get ultimaker.option.material_guid 2>/dev/null",
                      out, out_sz, DENEB_DEFAULT_MATERIAL_GUID);
}

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

    if (strcmp(instruction, "ABORT") == 0)
        return backend_zmq_abort();

    if (strcmp(instruction, "PREPARE") == 0) {
        return backend_zmq_send_job(job.path, "Cura", "deneb-current-job",
                                    0.0f, 0.0f);
    }

    return -1;
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
    char nozzle_id[24];
    char material_guid[48];
    read_nozzle_id(nozzle_id, sizeof(nozzle_id));
    read_material_guid(material_guid, sizeof(material_guid));

    json_key(w, "configuration");
    json_arr_open(w);
    json_obj_open(w);
    json_int(w, "extruder_index", 0);
    json_str(w, "print_core_id", nozzle_id);
    json_key(w, "material");
    json_obj_open(w);
    json_str(w, "guid", material_guid);
    json_str(w, "brand", DENEB_DEFAULT_MATERIAL_BRAND);
    json_str(w, "material", DENEB_DEFAULT_MATERIAL_TYPE);
    json_str(w, "color", DENEB_DEFAULT_MATERIAL_COLOR);
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

static int copy_tag_value(const char *xml, const char *tag, char *out, size_t out_sz)
{
    char open_tag[32];
    char close_tag[32];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strstr(xml, open_tag);
    if (!start) return -1;
    start += strlen(open_tag);
    const char *end = strstr(start, close_tag);
    if (!end || end <= start) return -1;

    size_t len = (size_t)(end - start);
    while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
    if (len == 0 || len >= out_sz) return -1;

    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int is_safe_guid(const char *guid)
{
    size_t len = strlen(guid);
    if (len < 32 || len >= 64) return 0;
    for (const char *p = guid; *p; p++) {
        if (!(isxdigit((unsigned char)*p) || *p == '-')) return 0;
    }
    return 1;
}

static int parse_material_file(const char *path, char *guid, size_t guid_sz, int *version)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char *xml = malloc(131073);
    if (!xml) {
        fclose(f);
        return -1;
    }

    size_t n = fread(xml, 1, 131072, f);
    fclose(f);
    xml[n] = '\0';

    char version_str[32];
    int ok = copy_tag_value(xml, "GUID", guid, guid_sz) == 0 &&
             copy_tag_value(xml, "version", version_str, sizeof(version_str)) == 0 &&
             is_safe_guid(guid);
    if (ok) {
        *version = atoi(version_str);
        ok = *version >= 0;
    }

    free(xml);
    return ok ? 0 : -1;
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

    if (parse_material_file(material_path, guid, sizeof(guid), &version) < 0) {
        fprintf(stderr, "deneb-api: material upload rejected: failed to parse %s\n", filename);
        unlink(material_path);
        return -1;
    }

    if (mkdir(DENEB_CLUSTER_MATERIAL_DIR, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "deneb-api: failed to create material catalog %s: %s\n",
                DENEB_CLUSTER_MATERIAL_DIR, strerror(errno));
        unlink(material_path);
        return -1;
    }

    char record_path[256];
    snprintf(record_path, sizeof(record_path), "%s/%s.json", DENEB_CLUSTER_MATERIAL_DIR, guid);
    FILE *out = fopen(record_path, "w");
    if (!out) {
        fprintf(stderr, "deneb-api: failed to write material record %s: %s\n",
                record_path, strerror(errno));
        unlink(material_path);
        return -1;
    }
    fprintf(out, "{\"guid\":\"%s\",\"version\":%d}", guid, version);
    fclose(out);
    unlink(material_path);
    fprintf(stderr, "deneb-api: accepted material %s version %d\n", guid, version);
    return 0;
}

static int append_str(char **buf, size_t *cap, size_t *pos, const char *s)
{
    size_t len = strlen(s);
    while (*pos + len + 1 > *cap) {
        size_t new_cap = *cap ? (*cap * 2) : 256;
        while (new_cap < *pos + len + 1) new_cap *= 2;

        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf)
            return -1;

        *buf = new_buf;
        *cap = new_cap;
    }

    memcpy(*buf + *pos, s, len);
    *pos += len;
    (*buf)[*pos] = '\0';
    return 0;
}

static void write_cluster_materials_response(http_response_t *resp)
{
    const size_t stock_len = strlen(DENEB_CLUSTER_MATERIALS_JSON);
    size_t cap = stock_len + 1024;
    char *body = malloc(cap);
    if (!body) {
        api_http_set_body_str(resp, DENEB_CLUSTER_MATERIALS_JSON);
        return;
    }

    size_t pos = stock_len > 0 ? stock_len - 1 : 0;
    memcpy(body, DENEB_CLUSTER_MATERIALS_JSON, pos);
    body[pos] = '\0';

    DIR *dir = opendir(DENEB_CLUSTER_MATERIAL_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (!strstr(ent->d_name, ".json")) continue;

            char path[256];
            snprintf(path, sizeof(path), "%s/%s", DENEB_CLUSTER_MATERIAL_DIR, ent->d_name);
            FILE *f = fopen(path, "r");
            if (!f) continue;

            char record[160];
            size_t n = fread(record, 1, sizeof(record) - 1, f);
            fclose(f);
            record[n] = '\0';
            if (n == 0 || record[0] != '{') continue;

            if (append_str(&body, &cap, &pos, ",") < 0 ||
                append_str(&body, &cap, &pos, record) < 0) {
                closedir(dir);
                resp->status_code = 500;
                api_http_set_body_str(resp, "{\"message\":\"Material catalog response too large\"}");
                free(body);
                return;
            }
        }
        closedir(dir);
    }

    if (append_str(&body, &cap, &pos, "]") < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Material catalog response too large\"}");
        free(body);
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
        if (serve_pending_cluster_job(resp))
            return;
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
    json_int(&w, "time_elapsed",
             deneb_print_elapsed_seconds(s->time_total, s->time_left));
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
    json_arr_str(&w, DENEB_CURA_MACHINE_FAMILY);
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

static void normalize_action_value(char *action)
{
    if (!action) return;
    char *start = action;
    char *end;
    while (*start && isspace((unsigned char)*start)) start++;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) end--;
    if (end <= start) {
        action[0] = '\0';
        return;
    }
    if ((*start == '"' && end[-1] == '"') || (*start == '\'' && end[-1] == '\'')) {
        start++;
        end--;
    }
    for (size_t i = 0; start + i < end; i++) {
        action[i] = (char)tolower((unsigned char)start[i]);
    }
    action[end - start] = '\0';
}

static int parse_action(const char *body, char *out, size_t out_sz)
{
    if (!body || !out || out_sz < 2) return -1;

    const char *p = strstr(body, "\"action\"");
    if (p) {
        p = strchr(p + 8, ':');
        if (!p) return -1;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p != '"' && *p != '\'') return -1;
        char quote = *p++;
        size_t i = 0;
        while (*p && *p != quote && i < out_sz - 1) {
            out[i++] = *p++;
        }
        if (*p != quote) return -1;
        out[i] = '\0';
        normalize_action_value(out);
        return 0;
    }

    /* Fallback: plain body value such as "print" or {"action":"print"} alternatives */
    while (*body && isspace((unsigned char)*body)) body++;
    if (!*body) return -1;

    size_t i = 0;
    while (*body && !isspace((unsigned char)*body) && *body != '{' && *body != '}' && i < out_sz - 1) {
        out[i++] = *body++;
    }
    out[i] = '\0';
    normalize_action_value(out);
    return *out ? 0 : -1;
}

static int action_wants_prepare(const char *action)
{
    return strcmp(action, "print") == 0 || strcmp(action, "resume") == 0 ||
           strcmp(action, "continue") == 0 || strcmp(action, "force") == 0 ||
           strcmp(action, "start") == 0;
}

static int action_is_abort(const char *action)
{
    return strcmp(action, "abort") == 0 || strcmp(action, "cancel") == 0;
}

static void log_cluster_action(const char *action, int has_pending_job, const char *path)
{
    fprintf(stderr, "deneb-api: cluster print action=%s path=%s has_pending=%s override=%s\n",
            action ? action : "(none)",
            path ? path : "(none)",
            has_pending_job ? "true" : "false",
            (action && strcmp(action, "force") == 0) ? "true" : "false");
}

void api_cluster_print_job_action_put(const http_request_t *req, http_response_t *resp)
{
    char action[16];
    int has_pending_job = pending_job_tracker() >= 0;
    if (!strstr(req->path, "/action")) {
        api_cluster_print_job_put(req, resp);
        return;
    }

    if (parse_action(req->body, action, sizeof(action)) < 0) {
        if (has_pending_job) {
            snprintf(action, sizeof(action), "print");
        } else {
            resp->status_code = 400;
            api_http_set_body_str(resp, "{\"message\":\"Expected {\\\"action\\\":\\\"pause|print|abort\\\"}\"}");
            return;
        }
    }

    log_cluster_action(action, has_pending_job, req->path);

    if (action_wants_prepare(action) && has_pending_job) {
        if (send_pending_job_instruction("PREPARE") != 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to continue print\"}");
            return;
        }
        /* Keep pending print metadata visible during preheat / queued startup. */
    } else if (action_is_abort(action) && has_pending_job) {
        if (send_pending_job_instruction("ABORT") != 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to cancel print\"}");
            return;
        }
        unlink(DENEB_PENDING_JOB_PATH);
    } else if (strcmp(action, "pause") == 0) {
        if (backend_zmq_pause() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to pause print\"}");
            return;
        }
    } else if (strcmp(action, "print") == 0 || strcmp(action, "resume") == 0 ||
               strcmp(action, "continue") == 0 || strcmp(action, "force") == 0 ||
               strcmp(action, "start") == 0) {
        if (backend_zmq_resume() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to resume print\"}");
            return;
        }
    } else if (action_is_abort(action)) {
        if (backend_zmq_abort() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
            return;
        }
        unlink(DENEB_PENDING_JOB_PATH);
    } else if (strcmp(action, "stop") == 0) {
        if (backend_zmq_stop_print() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to stop print\"}");
            return;
        }
        unlink(DENEB_PENDING_JOB_PATH);
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
        unlink(DENEB_PENDING_JOB_PATH);
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
