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

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#ifndef DENEB_CLUSTER_UCI_CONFIG_DIR
#define DENEB_CLUSTER_UCI_CONFIG_DIR "/etc/config"
#endif
#ifndef DENEB_CLUSTER_UCI_DELTA_DIR
#define DENEB_CLUSTER_UCI_DELTA_DIR "/tmp/.uci"
#endif
#ifndef DENEB_CLUSTER_UCI_OVERRIDE_DIR
#define DENEB_CLUSTER_UCI_OVERRIDE_DIR "/var/run/uci"
#endif

/* UCI reads may see the committed, staged, or runtime override package. */
static const char *const configuration_files[] = {
    DENEB_CLUSTER_UCI_CONFIG_DIR "/ultimaker",
    DENEB_CLUSTER_UCI_CONFIG_DIR "/deneb",
    DENEB_CLUSTER_UCI_DELTA_DIR "/ultimaker",
    DENEB_CLUSTER_UCI_DELTA_DIR "/deneb",
    DENEB_CLUSTER_UCI_OVERRIDE_DIR "/ultimaker",
    DENEB_CLUSTER_UCI_OVERRIDE_DIR "/deneb",
};

enum { CONFIGURATION_FILE_COUNT = sizeof(configuration_files) / sizeof(configuration_files[0]),
       CONFIGURATION_MAX_AGE_SECONDS = 5,
       CONFIGURATION_FAILURE_LIMIT = 2 };

typedef struct {
    dev_t device;
    ino_t inode;
    off_t size;
    struct timespec changed;
    int error;
    bool present;
} configuration_file_version_t;

typedef struct {
    char friendly_name[64];
    char guid[48];
    char current_firmware[32];
    char latest_firmware[32];
    char nozzle_id[24];
    char material_guid[48];
} cluster_configuration_t;

/* Only the cluster printer route in the API loop reads and writes this cache. */
static struct {
    configuration_file_version_t files[CONFIGURATION_FILE_COUNT];
    struct timespec last_attempt;
    bool timed_attempt;
    bool published;
    unsigned failures;
    char hostname[64];
    cluster_configuration_t snapshot;
} configuration;

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
    struct ifaddrs *interfaces;
    snprintf(dst, dst_size, "127.0.0.1");
    if (getifaddrs(&interfaces) != 0)
        return;
    for (const struct ifaddrs *ifa = interfaces; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        const struct sockaddr_in *address = (const struct sockaddr_in *)ifa->ifa_addr;
        if ((ntohl(address->sin_addr.s_addr) >> 24) == 127)
            continue;
        if (inet_ntop(AF_INET, &address->sin_addr, dst, dst_size))
            break;
    }
    freeifaddrs(interfaces);
}

typedef enum {
    UCI_VALUE_FOUND,
    UCI_VALUE_MISSING,
    UCI_VALUE_ERROR,
} uci_value_result_t;

static uci_value_result_t read_uci_value(const char *option, char *dst, size_t dst_size)
{
    char cmd[128];
    char line[128];

    dst[0] = '\0';
    snprintf(cmd, sizeof(cmd), "LC_ALL=C exec uci get %s 2>&1", option);
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return UCI_VALUE_ERROR;

    bool got_line = fgets(line, sizeof(line), fp) != NULL;
    bool extra_output = false;
    bool terminated = got_line && strchr(line, '\n');
    for (int ch; (ch = fgetc(fp)) != EOF;) {
        extra_output = true;
        if (ch == '\n')
            terminated = true;
    }
    bool read_error = ferror(fp);
    int status = pclose(fp);
    if (read_error || !got_line || status == -1 || !WIFEXITED(status))
        return UCI_VALUE_ERROR;

    /* OpenWrt's CLI returns 1 for both missing entries and read/parse errors. */
    if (WEXITSTATUS(status) == 1 && !extra_output &&
        strcmp(line, "uci: Entry not found\n") == 0)
        return UCI_VALUE_MISSING;
    if (WEXITSTATUS(status) != 0 || !terminated)
        return UCI_VALUE_ERROR;

    size_t length = strcspn(line, "\n");
    if (length >= dst_size)
        length = dst_size - 1;
    memcpy(dst, line, length);
    dst[length] = '\0';
    return UCI_VALUE_FOUND;
}

static bool read_firmware_setting(const char *option, const char *fallback,
                                   char *dst, size_t dst_size)
{
    uci_value_result_t result = read_uci_value(option, dst, dst_size);
    dst[strcspn(dst, "\r")] = '\0';
    if (!dst[0])
        snprintf(dst, dst_size, "%s", fallback);
    return result != UCI_VALUE_ERROR;
}

static bool load_configuration(cluster_configuration_t *next, const char *hostname)
{
    char name[128];
    char guid[96];
    char nozzle[16];
    bool complete = read_uci_value("ultimaker.option.printer_name",
                                   name, sizeof(name)) != UCI_VALUE_ERROR;
    deneb_printer_identity_copy_line_or_default(name, hostname, next->friendly_name,
                                                sizeof(next->friendly_name));
    uci_value_result_t guid_result = read_uci_value("ultimaker.option.host_guid",
                                                   guid, sizeof(guid));
    if (guid_result == UCI_VALUE_MISSING)
        guid_result = read_uci_value("deneb.system.guid", guid, sizeof(guid));
    complete &= guid_result != UCI_VALUE_ERROR;
    deneb_printer_identity_normalize_guid(guid, next->guid, sizeof(next->guid));

    complete &= read_firmware_setting("ultimaker.version.nr", DENEB_VERSION,
                                      next->current_firmware, sizeof(next->current_firmware));
    complete &= read_firmware_setting("ultimaker.version.latest", " ",
                                      next->latest_firmware, sizeof(next->latest_firmware));
    complete &= read_uci_value("ultimaker.option.nozzle_size",
                               nozzle, sizeof(nozzle)) != UCI_VALUE_ERROR;
    deneb_print_profile_normalize_nozzle_id(nozzle, next->nozzle_id, sizeof(next->nozzle_id));
    complete &= read_uci_value("ultimaker.option.material_guid", next->material_guid,
                               sizeof(next->material_guid)) != UCI_VALUE_ERROR;
    if (!next->material_guid[0]) {
        snprintf(next->material_guid, sizeof(next->material_guid), "%s",
                 DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID);
    }
    if (!deneb_print_is_cluster_guid(next->material_guid))
        next->material_guid[0] = '\0';

    return complete;
}

static void refresh_configuration(const char *hostname)
{
    struct timespec now;
    bool clock_valid = clock_gettime(CLOCK_MONOTONIC, &now) == 0;
    bool unchanged = strcmp(hostname, configuration.hostname) == 0;
    bool stat_complete = true;

    /* Observe the versions before reading UCI. A concurrent write then
     * invalidates any snapshot that raced it on the next request. */
    for (size_t i = 0; i < CONFIGURATION_FILE_COUNT; i++) {
        struct stat st;
        configuration_file_version_t observed = {0};
        if (stat(configuration_files[i], &st) == 0) {
            observed = (configuration_file_version_t){
                .device = st.st_dev, .inode = st.st_ino, .size = st.st_size,
                .changed = st.st_ctim, .present = true,
            };
        } else if (errno != ENOENT && errno != ENOTDIR) {
            observed.error = errno;
            stat_complete = false;
        }
        configuration_file_version_t *cached = &configuration.files[i];
        if (cached->present != observed.present || cached->device != observed.device ||
            cached->inode != observed.inode || cached->size != observed.size ||
            cached->error != observed.error ||
            cached->changed.tv_sec != observed.changed.tv_sec ||
            cached->changed.tv_nsec != observed.changed.tv_nsec)
            unchanged = false;
        *cached = observed;
    }
    /* One immediate retry handles an isolated failure. Subsequent failures
     * reuse the refresh interval until the observed configuration changes. */
    time_t elapsed = clock_valid ? now.tv_sec - configuration.last_attempt.tv_sec : 0;
    if (clock_valid && now.tv_nsec < configuration.last_attempt.tv_nsec)
        elapsed--;
    bool recent = clock_valid && configuration.timed_attempt &&
                  elapsed < CONFIGURATION_MAX_AGE_SECONDS;
    if (unchanged && recent && configuration.failures != 1)
        return;

    if (!unchanged) {
        configuration.failures = 0;
        snprintf(configuration.hostname, sizeof(configuration.hostname), "%s", hostname);
    }
    cluster_configuration_t next = {0};
    bool complete = load_configuration(&next, hostname);
    complete &= stat_complete;
    if (complete || !configuration.published)
        configuration.snapshot = next;
    if (complete) {
        configuration.published = true;
        configuration.failures = 0;
    } else if (configuration.failures < CONFIGURATION_FAILURE_LIMIT) {
        configuration.failures++;
    }
    /* A slow UCI command must not consume the cooldown after a failure. */
    configuration.timed_attempt =
        clock_gettime(CLOCK_MONOTONIC, &configuration.last_attempt) == 0;
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
    char material_type[32];
    char material_color[32];
    const char *material_guid = configuration.snapshot.material_guid;
    deneb_print_profile_material_type_from_guid(material_guid, material_type,
                                                sizeof(material_type));
    deneb_print_profile_material_color_from_guid(material_guid, material_color,
                                                 sizeof(material_color));

    json_key(w, "configuration");
    json_arr_open(w);
    json_obj_open(w);
    json_int(w, "extruder_index", 0);
    json_str(w, "print_core_id", configuration.snapshot.nozzle_id);
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
    char ip_address[64];
    const char *firmware_update_status = "up_to_date";
    char buf[2048];
    json_writer_t w;

    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    refresh_configuration(hostname);
    read_local_ip(ip_address, sizeof(ip_address));
    if (strcmp(configuration.snapshot.latest_firmware, " ") != 0 &&
        strcmp(configuration.snapshot.latest_firmware, configuration.snapshot.current_firmware) != 0)
        firmware_update_status = "update_available";

    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);
    json_obj_open(&w);
    json_bool(&w, "enabled", 1);
    json_str(&w, "friendly_name", configuration.snapshot.friendly_name);
    json_str(&w, "machine_variant", DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    json_str(&w, "status",
             deneb_print_cluster_printer_status_label(
                 backend_zmq_get_status_label()));
    json_str(&w, "unique_name", hostname);
    json_str(&w, "uuid", configuration.snapshot.guid);
    write_configuration(&w);
    json_str(&w, "firmware_version", configuration.snapshot.current_firmware);
    json_str(&w, "ip_address", ip_address);
    json_str(&w, "firmware_update_status", firmware_update_status);
    json_str(&w, "latest_available_firmware", configuration.snapshot.latest_firmware);
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
