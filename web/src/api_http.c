/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * HTTP request parsing, routing, and response serialization.
 * Implements the UltiMaker REST API v1 endpoint map.
 */

#include "api_http.h"
#include "api_printer.h"
#include "api_print_job.h"
#include "api_system.h"
#include "api_materials.h"
#include "api_auth.h"
#include "api_deneb.h"
#include "api_cluster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* Forward declarations for route handlers */
static void handle_not_found(const http_request_t *req, http_response_t *resp);
static void handle_method_not_allowed(const http_request_t *req, http_response_t *resp);

/* Route table */
static const api_route_t routes[] = {
    /* Printer endpoints */
    {"GET",  "/api/v1/printer",                                    api_printer_get,           0},
    {"GET",  "/api/v1/printer/status",                             api_printer_status_get,    0},
    {"GET",  "/api/v1/printer/bed",                                api_printer_bed_get,       0},
    {"GET",  "/api/v1/printer/bed/temperature",                    api_printer_bed_temp_get,  0},
    {"GET",  "/api/v1/printer/bed/type",                           api_printer_bed_type_get,  0},
    {"GET",  "/api/v1/printer/bed/pre_heat",                       api_printer_bed_preheat_get, 0},
    {"GET",  "/api/v1/printer/heads",                              api_printer_heads_get,     0},
    {"GET",  "/api/v1/printer/heads/0",                            api_printer_head_get,      0},
    {"GET",  "/api/v1/printer/heads/0/position",                   api_printer_position_get,  0},
    {"GET",  "/api/v1/printer/heads/0/extruders",                  api_printer_extruders_get, 0},
    {"GET",  "/api/v1/printer/heads/0/extruders/0",                api_printer_extruder_get,  0},
    {"GET",  "/api/v1/printer/heads/0/extruders/0/hotend",         api_printer_hotend_get,    0},
    {"GET",  "/api/v1/printer/heads/0/extruders/0/hotend/temperature", api_printer_hotend_temp_get, 0},
    {"GET",  "/api/v1/printer/heads/0/extruders/0/feeder",         api_printer_feeder_get,    0},
    {"GET",  "/api/v1/printer/heads/0/extruders/0/active_material",api_printer_material_get,  0},
    {"GET",  "/api/v1/printer/led",                                api_printer_led_get,       0},
    {"GET",  "/api/v1/printer/led/brightness",                     api_printer_led_brightness_get, 0},
    {"GET",  "/api/v1/printer/led/hue",                            api_printer_led_hue_get,    0},
    {"GET",  "/api/v1/printer/led/saturation",                     api_printer_led_saturation_get, 0},
    {"GET",  "/api/v1/printer/network",                            api_printer_network_get,   0},

    /* Print job endpoints */
    {"GET",  "/api/v1/print_job",                                  api_print_job_get,         0},
    {"GET",  "/api/v1/print_job/state",                            api_print_job_state_get,   0},
    {"GET",  "/api/v1/print_job/progress",                         api_print_job_progress_get,0},
    {"GET",  "/api/v1/print_job/time_elapsed",                     api_print_job_time_elapsed_get, 0},
    {"GET",  "/api/v1/print_job/time_total",                       api_print_job_time_total_get, 0},
    {"GET",  "/api/v1/print_job/name",                             api_print_job_name_get,    0},
    {"GET",  "/api/v1/print_job/uuid",                             api_print_job_uuid_get,    0},
    {"GET",  "/api/v1/print_job/source",                           api_print_job_source_get,  0},
    {"GET",  "/api/v1/print_job/datetime_started",                 api_print_job_datetime_started_get, 0},
    {"GET",  "/api/v1/print_job/datetime_finished",                api_print_job_datetime_finished_get, 0},

    /* System endpoints */
    {"GET",  "/api/v1/system",                                     api_system_get,            0},
    {"GET",  "/api/v1/system/name",                                api_system_name_get,       0},
    {"GET",  "/api/v1/system/hostname",                            api_system_hostname_get,   0},
    {"GET",  "/api/v1/system/firmware",                            api_system_firmware_get,   0},
    {"GET",  "/api/v1/system/hardware",                            api_system_hardware_get,   0},
    {"GET",  "/api/v1/system/type",                                api_system_type_get,       0},
    {"GET",  "/api/v1/system/variant",                             api_system_variant_get,    0},
    {"GET",  "/api/v1/system/platform",                            api_system_platform_get,   0},
    {"GET",  "/api/v1/system/memory",                              api_system_memory_get,     0},
    {"GET",  "/api/v1/system/uptime",                              api_system_uptime_get,     0},
    {"GET",  "/api/v1/system/language",                            api_system_language_get,   0},
    {"GET",  "/api/v1/system/country",                             api_system_country_get,    0},
    {"GET",  "/api/v1/system/time",                                api_system_time_get,       0},
    {"GET",  "/api/v1/system/guid",                                api_system_guid_get,       0},
    {"GET",  "/api/v1/system/log",                                 api_system_log_get,        0},

    /* Materials endpoints */
    {"GET",  "/api/v1/materials",                                  api_materials_get,         0},

    /* Auth endpoints */
    {"POST", "/api/v1/auth/request",                               api_auth_request_post,     0},
    {"GET",  "/api/v1/auth/check/",                                api_auth_check_get,        0},  /* prefix match */
    {"GET",  "/api/v1/auth/verify",                                api_auth_verify_get,       1},

    /* Ambient temperature */
    {"GET",  "/api/v1/ambient_temperature",                        api_printer_ambient_get,   0},

    /* Air manager */
    {"GET",  "/api/v1/airmanager",                                 api_printer_airmanager_get,0},

    /* Deneb extensions */
    {"GET",  "/api/v1/deneb/events",                               api_deneb_events_get,      0},
    {"GET",  "/api/v1/deneb/version",                              api_deneb_version_get,     0},
    {"GET",  "/api/v1/deneb/locale/",                              api_deneb_locale_get,      0},  /* prefix: /deneb/locale/{lang} */
    {"POST", "/api/v1/deneb/setup",                                api_deneb_setup_post,      0},
    {"POST", "/api/v1/deneb/auth",                                 api_deneb_auth_post,       0},
    {"GET",  "/api/v1/deneb/config",                               api_deneb_config_get,      0},
    {"GET",  "/api/v1/deneb/print_jobs",                           api_deneb_print_jobs_get,  0},

    /* Cura local cluster API endpoints */
    {"GET",  "/cluster-api/v1/materials",                           api_cluster_materials_get, 0},
    {"POST", "/cluster-api/v1/materials",                           api_cluster_materials_get, 0},
    {"GET",  "/cluster-api/v1/materials/",                          api_cluster_materials_get, 0},
    {"POST", "/cluster-api/v1/materials/",                          api_cluster_materials_get, 0},
    {"GET",  "/cluster-api/v1/printers",                            api_cluster_printers_get,  0},
    {"GET",  "/cluster-api/v1/print_jobs",                          api_cluster_print_jobs_get, 0},
    {"GET",  "/cluster-api/v1/print_jobs/",                         api_cluster_print_job_preview_get, 0},
    {"POST", "/cluster-api/v1/print_jobs/",                         api_cluster_print_jobs_post, 0},
    {"PUT",  "/cluster-api/v1/print_jobs/",                         api_cluster_print_job_action_put, 0},
    {"DELETE", "/cluster-api/v1/print_jobs/",                       api_cluster_print_job_delete, 0},

    /* M7 write endpoints (PUT/POST) */
    {"PUT",  "/api/v1/print_job/state",                            api_print_job_state_put,   1},
    {"POST", "/api/v1/print_job",                                  api_print_job_post,        1},
    {"PUT",  "/api/v1/printer/bed/temperature",                    api_printer_bed_temp_put,  1},
    {"PUT",  "/api/v1/printer/heads/0/extruders/0/hotend/temperature", api_printer_hotend_temp_put, 1},
    {"PUT",  "/api/v1/printer/bed/pre_heat",                       api_printer_bed_preheat_put, 1},
    {"PUT",  "/api/v1/printer/heads/0/position",                   api_printer_position_put,  1},
    {"POST", "/api/v1/printer/heads/0/position",                   api_printer_position_post, 1},
    {"PUT",  "/api/v1/printer/led",                                api_printer_led_put,       1},
    {"PUT",  "/api/v1/printer/led/brightness",                     api_printer_led_brightness_put, 1},
    {"POST", "/api/v1/printer/validate_header",                    api_printer_validate_post, 1},

    /* CORS preflight (OPTIONS) - handled generically below */
    {NULL, NULL, NULL, 0}
};

void api_http_init(void)
{
    api_auth_init();
}

int api_http_parse(http_request_t *req, const char *raw, size_t len)
{
    memset(req, 0, sizeof(*req));

    /* Parse request line: METHOD PATH HTTP/1.x */
    const char *p = raw;
    const char *end = memchr(p, ' ', len);
    if (!end || (size_t)(end - p) >= sizeof(req->method)) return -1;
    memcpy(req->method, p, (size_t)(end - p));
    req->method[end - p] = '\0';
    p = end + 1;

    /* Path */
    end = memchr(p, ' ', (size_t)(raw + len - p));
    if (!end || (size_t)(end - p) >= sizeof(req->path)) return -1;
    memcpy(req->path, p, (size_t)(end - p));
    req->path[end - p] = '\0';

    /* Split query string */
    char *q = strchr(req->path, '?');
    if (q) {
        strncpy(req->query, q + 1, sizeof(req->query) - 1);
        *q = '\0';
    }

    /* Skip to headers */
    p = end + 1;
    const char *line_end = strstr(p, "\r\n");
    if (!line_end) line_end = strstr(p, "\n");
    if (line_end) p = line_end + (line_end[0] == '\r' ? 2 : 1);

    /* Parse headers */
    req->header_count = 0;
    while (p < raw + len && req->header_count < HTTP_MAX_HEADERS) {
        line_end = strstr(p, "\r\n");
        if (!line_end) line_end = strstr(p, "\n");
        if (!line_end || line_end == p) break;

        const char *colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon) {
            size_t name_len = (size_t)(colon - p);
            if (name_len >= sizeof(req->headers[0].name)) name_len = sizeof(req->headers[0].name) - 1;
            memcpy(req->headers[req->header_count].name, p, name_len);
            req->headers[req->header_count].name[name_len] = '\0';

            p = colon + 1;
            while (p < line_end && *p == ' ') p++;
            size_t val_len = (size_t)(line_end - p);
            if (val_len >= sizeof(req->headers[0].value)) val_len = sizeof(req->headers[0].value) - 1;
            memcpy(req->headers[req->header_count].value, p, val_len);
            req->headers[req->header_count].value[val_len] = '\0';

            /* Extract specific headers */
            if (strcasecmp(req->headers[req->header_count].name, "Authorization") == 0) {
                strncpy(req->auth_digest, req->headers[req->header_count].value, sizeof(req->auth_digest) - 1);
            } else if (strcasecmp(req->headers[req->header_count].name, "Content-Type") == 0) {
                strncpy(req->content_type, req->headers[req->header_count].value, sizeof(req->content_type) - 1);
                /* Parse multipart boundary */
                const char *bp = strstr(req->headers[req->header_count].value, "boundary=");
                if (bp) {
                    bp += 9;
                    if (*bp == '"') bp++;
                    const char *end = strchr(bp, ';');
                    if (!end) end = bp + strlen(bp);
                    if (end > bp && end[-1] == '"') end--;
                    size_t blen = (size_t)(end - bp);
                    if (blen >= sizeof(req->multipart_boundary)) blen = sizeof(req->multipart_boundary) - 1;
                    memcpy(req->multipart_boundary, bp, blen);
                    req->multipart_boundary[blen] = '\0';
                    req->is_multipart = 1;
                }
            } else if (strcasecmp(req->headers[req->header_count].name, "Content-Length") == 0) {
                long cl = strtol(req->headers[req->header_count].value, NULL, 10);
                req->content_length = (cl < 0 || cl > 52428800) ? 0 : (int)cl;  /* cap at 50MB */
            }

            req->header_count++;
        }

        p = line_end + (line_end[0] == '\r' ? 2 : 1);
    }

    /* Find body (after double CRLF) */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (!body_start) body_start = strstr(raw, "\n\n");
    if (body_start) {
        body_start += (body_start[0] == '\r' ? 4 : 2);
        size_t body_avail = (size_t)(raw + len - body_start);
        if (body_avail > HTTP_MAX_BODY) body_avail = HTTP_MAX_BODY;
        memcpy(req->body, body_start, body_avail);
        req->body_len = (int)body_avail;
    }

    return 0;
}

static int match_path(const char *pattern, const char *path)
{
    /* Exact match */
    if (strcmp(pattern, path) == 0) return 1;
    /* Prefix match for patterns ending with / */
    size_t plen = strlen(pattern);
    if (plen > 0 && pattern[plen - 1] == '/') {
        return strncmp(pattern, path, plen) == 0;
    }
    return 0;
}

void api_http_route(const http_request_t *req, http_response_t *resp)
{
    /* Find matching route */
    for (const api_route_t *r = routes; r->method; r++) {
        if (strcmp(r->method, req->method) != 0) continue;
        if (match_path(r->pattern, req->path)) {
            /* Check auth if required */
            if (r->requires_auth && !api_http_check_auth(req)) {
                resp->status_code = 401;
                /* Send Digest auth challenge so Cura can authenticate */
                char challenge[256];
                snprintf(challenge, sizeof(challenge),
                    "Digest realm=\"%s\", nonce=\"%s\", qop=\"auth\"",
                    api_auth_get_realm(), api_auth_get_nonce());
                strncpy(resp->auth_challenge, challenge, sizeof(resp->auth_challenge) - 1);
                api_http_set_body_str(resp, "{\"message\":\"Unauthorized\"}");
                return;
            }
            r->handler(req, resp);
            return;
        }
    }

    /* Handle OPTIONS for CORS preflight */
    if (strcmp(req->method, "OPTIONS") == 0) {
        resp->status_code = 204;
        return;
    }

    /* Try method mismatch */
    for (const api_route_t *r = routes; r->method; r++) {
        if (strcmp(r->pattern, req->path) == 0) {
            handle_method_not_allowed(req, resp);
            return;
        }
    }

    handle_not_found(req, resp);
}

int api_http_serialize(const http_response_t *resp, char *buf, size_t cap)
{
    const char *status_text;
    switch (resp->status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 204: status_text = "No Content"; break;
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 409: status_text = "Conflict"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 501: status_text = "Not Implemented"; break;
        case 503: status_text = "Service Unavailable"; break;
        default: status_text = "OK"; break;
    }

    /* Clamp Content-Length to what actually fits in the buffer */
    size_t max_body = cap > 512 ? cap - 512 : 0;  /* reserve space for headers */
    size_t actual_body_len = resp->body_len < max_body ? resp->body_len : max_body;

    int n;
    if (resp->is_sse) {
        n = snprintf(buf, cap,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-store\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
            "%s%s"
            "\r\n",
            resp->status_code, status_text,
            resp->content_type[0] ? resp->content_type : "text/event-stream",
            resp->auth_challenge[0] ? "WWW-Authenticate: " : "",
            resp->auth_challenge);
    } else {
        n = snprintf(buf, cap,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
        "%s%s"
        "\r\n",
        resp->status_code, status_text,
        resp->content_type[0] ? resp->content_type : "application/json",
        actual_body_len,
        resp->keep_alive ? "keep-alive" : "close",
        resp->auth_challenge[0] ? "WWW-Authenticate: " : "",
        resp->auth_challenge);
    }

    if (n < 0 || (size_t)n >= cap) n = (int)cap - 1;
    if (resp->body && actual_body_len > 0) {
        memcpy(buf + n, resp->body, actual_body_len);
        n += (int)actual_body_len;
    }

    return n;
}

void api_http_response_init(http_response_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->status_code = 200;
    strcpy(resp->content_type, "application/json");
}

void api_http_set_body(http_response_t *resp, const char *data, size_t len)
{
    if (resp->body) free(resp->body);
    resp->body = malloc(len + 1);
    if (resp->body) {
        memcpy(resp->body, data, len);
        resp->body[len] = '\0';
        resp->body_len = len;
    } else {
        resp->body_len = 0;
    }
}

void api_http_set_body_str(http_response_t *resp, const char *str)
{
    api_http_set_body(resp, str, strlen(str));
}

int api_http_check_auth(const http_request_t *req)
{
    /* Check Bearer token (web UI auth) */
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "Authorization") == 0) {
            if (strncasecmp(req->headers[i].value, "Bearer ", 7) == 0) {
                return api_auth_validate_token(req->headers[i].value + 7);
            }
            if (strncasecmp(req->headers[i].value, "Digest ", 7) == 0) {
                return api_auth_validate_digest(req->headers[i].value + 7, req->method, req->path);
            }
        }
    }
    /* Check if auth is disabled */
    return api_auth_is_disabled();
}

static void handle_not_found(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status_code = 404;
    api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
}

static void handle_method_not_allowed(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status_code = 405;
    api_http_set_body_str(resp, "{\"message\":\"Method not allowed\"}");
}
