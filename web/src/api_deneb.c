/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb extension endpoints. /api/v1/deneb/*
 * SSE events, version info, setup, session auth.
 */

#include "api_deneb.h"
#include "backend_zmq.h"
#include "json_file.h"
#include "json_field.h"
#include "json_writer.h"
#include "pending_job_file.h"
#include "print_history.h"
#include "print_job_summary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "api_auth.h"

static void generate_token(char *out, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "api_deneb: /dev/urandom unavailable!\n");
        for (size_t i = 0; i < len - 1; i++) out[i] = '0';
    } else {
        unsigned char buf[64];
        ssize_t r = read(fd, buf, sizeof(buf));
        close(fd);
        if (r < 0) r = 0;
        for (size_t i = 0; i < len - 1 && i < (size_t)r; i++)
            out[i] = "0123456789abcdef"[buf[i] & 0x0f];
    }
    out[len - 1] = '\0';
}

/* SSE: /api/v1/deneb/events */
void api_deneb_events_get(const http_request_t *req, http_response_t *resp)
{
    /* Validate token from query param if auth is enabled */
    if (!api_auth_is_disabled()) {
        const char *token = strstr(req->query, "token=");
        if (token) {
            token += 6;
            char tok[64];
            size_t i = 0;
            while (token[i] && token[i] != '&' && i < sizeof(tok) - 1) { tok[i] = token[i]; i++; }
            tok[i] = '\0';
            if (!api_auth_validate_token(tok)) {
                resp->status_code = 401;
                api_http_set_body_str(resp, "event: error\ndata: {\"message\":\"Unauthorized\"}\n\n");
                return;
            }
        } else {
            resp->status_code = 401;
            api_http_set_body_str(resp, "event: error\ndata: {\"message\":\"Unauthorized\"}\n\n");
            return;
        }
    }

    /*
     * SSE endpoint. For now, return the current status as a single event.
     * A full implementation would keep the connection open and stream
     * updates. This is handled specially by the main loop's SSE client
     * tracking, but for the initial response we send one event.
     */
    /* Mark as SSE — main.c will keep the connection open and push events */
    resp->is_sse = 1;
    resp->keep_alive = 1;
    strcpy(resp->content_type, "text/event-stream");

    /* Send initial event with current status */
    const char *json = backend_zmq_get_status_json();
    size_t json_len = strlen(json);
    size_t total = json_len + 16;
    char *buf = malloc(total);
    if (!buf) {
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Out of memory\"}");
        return;
    }
    int n = snprintf(buf, total, "data: %s\n\n", json);
    api_http_set_body(resp, buf, (size_t)n);
    free(buf);
}

/* Version: /api/v1/deneb/version */
void api_deneb_version_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[256];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "deneb_version", DENEB_VERSION);
    json_str(&w, "api_version", "1.0.0");
    json_str(&w, "printer_type", "Ultimaker 2+ Connect");
    json_bool(&w, "setup_complete", api_auth_is_setup_complete());
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_deneb_print_backend_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[512];
    const char *backend = backend_zmq_get_print_backend_name();
    json_writer_t w;

    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "print_backend", backend);
    json_str(&w, "status_url", backend_zmq_get_print_backend_status_url());
    json_str(&w, "command_url", backend_zmq_get_print_backend_command_url());
    json_bool(&w, "native_printsvc", strcmp(backend, "native") == 0);
    json_bool(&w, "stock_coordinator_route", strcmp(backend, "coordinator") == 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

/* Setup: /api/v1/deneb/setup (POST) */
void api_deneb_setup_post(const http_request_t *req, http_response_t *resp)
{
    /*
     * Body: {"password": "...", "auth_required": true/false}
     * Or:   {"password": "..."} (defaults to auth_required: true)
     *
     * After initial setup, requires valid auth token to change settings.
     */
    int already_setup = api_auth_is_setup_complete();

    if (already_setup && !api_http_check_auth(req)) {
        resp->status_code = 401;
        api_http_set_body_str(resp, "{\"message\":\"Unauthorized\"}");
        return;
    }

    const char *body = req->body;

    char password[128];
    int has_password = (deneb_json_get_value(body, "password", password, sizeof(password)) == 0);

    /* Parse auth_required */
    int auth_required = 1; /* default: enable auth */
    deneb_json_get_bool_value(body, "auth_required", &auth_required);

    if (auth_required && (!has_password || password[0] == '\0') && !api_auth_has_password()) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Missing password field\"}");
        return;
    }

    /* Store password when supplied; auth-only updates can reuse the current hash. */
    if (auth_required && has_password && password[0] != '\0') {
        api_auth_set_password(password);
    }
    api_auth_set_setup_complete();
    api_auth_set_disabled(!auth_required);

    /* Generate session token for immediate use */
    char token[64];
    generate_token(token, sizeof(token));
    api_auth_add_session_token(token, 30);

    char buf[160];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "token", token);
    json_int(&w, "expires", (long long)(time(NULL) + 30 * 86400));
    json_bool(&w, "auth_enabled", auth_required);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

/* Auth: /api/v1/deneb/auth (POST) */
void api_deneb_auth_post(const http_request_t *req, http_response_t *resp)
{
    /*
     * Body: {"password": "..."}
     * Returns: {"token": "...", "expires": ...}
     */
    const char *body = req->body;

    char password[128];
    if (deneb_json_get_value(body, "password", password, sizeof(password)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Missing password\"}");
        return;
    }

    if (!api_auth_check_password(password)) {
        resp->status_code = 401;
        api_http_set_body_str(resp, "{\"message\":\"Invalid password\"}");
        return;
    }

    char token[64];
    generate_token(token, sizeof(token));
    api_auth_add_session_token(token, 30);

    char buf[160];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "token", token);
    json_int(&w, "expires", (long long)(time(NULL) + 30 * 86400));
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

/* Locale: /api/v1/deneb/locale/{lang} (GET) */
void api_deneb_locale_get(const http_request_t *req, http_response_t *resp)
{
    /* Extract lang from path: /api/v1/deneb/locale/{lang} */
    const char *lang = req->path + strlen("/api/v1/deneb/locale/");
    if (!*lang) lang = "en";

    /* Sanitize: only allow alphanumeric and hyphens */
    char safe_lang[16];
    size_t i = 0;
    while (*lang && i < sizeof(safe_lang) - 1 &&
           ((*lang >= 'a' && *lang <= 'z') || (*lang >= 'A' && *lang <= 'Z') ||
            (*lang >= '0' && *lang <= '9') || *lang == '-')) {
        safe_lang[i++] = *lang++;
    }
    safe_lang[i] = '\0';
    if (i == 0) { resp->status_code = 404; api_http_set_body_str(resp, "{}"); return; }

    char path[128];
    snprintf(path, sizeof(path), "/www/deneb/locales/%s.json", safe_lang);

    FILE *f = fopen(path, "r");
    if (!f) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Locale not found\"}");
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Cap at 64KB to prevent OOM on corrupted files */
    if (sz > 65536) { fclose(f); resp->status_code = 500; api_http_set_body_str(resp, "{}"); return; }

    char *buf = malloc((size_t)sz + 1);
    if (buf) {
        fread(buf, 1, (size_t)sz, f);
        buf[sz] = '\0';
        api_http_set_body(resp, buf, (size_t)sz);
        free(buf);
    }
    fclose(f);
}

/* Config: /api/v1/deneb/config (GET) */
void api_deneb_config_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char lang[16] = "en";
    FILE *f = popen("uci -q get deneb.system.language 2>/dev/null", "r");
    if (f) {
        if (fgets(lang, sizeof(lang), f)) {
            char *nl = strchr(lang, '\n');
            if (nl) *nl = '\0';
        }
        pclose(f);
    }

    char buf[256];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "language", lang);
    json_bool(&w, "auth_enabled", !api_auth_is_disabled());
    json_int(&w, "max_sse_clients", 4);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_deneb_print_jobs_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    static char buf[32768];
    static char file_buf[16384];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);

    /* Current job */
    const printer_state_t *s = backend_zmq_get_state();
    deneb_print_job_summary_t summary;
    deneb_print_job_summary_init(&summary, s->filename, s->uuid, s->source,
                                 s->has_error, s->is_paused, s->is_printing,
                                 s->time_total, s->time_left, s->progress);
    if (summary.active) {
        json_key(&w, "current");
        json_obj_open(&w);
        json_str(&w, "name", summary.name);
        json_str(&w, "uuid", summary.uuid);
        json_str(&w, "source", summary.source);
        json_str(&w, "state", summary.state);
        json_float(&w, "progress", summary.progress_percent);
        json_int(&w, "time_total", summary.time_total);
        json_int(&w, "time_elapsed", summary.time_elapsed);
        json_int(&w, "time_left", summary.time_left);
        json_obj_close(&w);
    } else {
        json_null(&w, "current");
    }

    /* Pending jobs */
    deneb_json_file_read_array_or_empty(DENEB_PENDING_JOB_PATH, file_buf, sizeof(file_buf));
    json_raw(&w, "pending", file_buf);

    /* History */
    deneb_print_history_read_default_array_or_empty(file_buf, sizeof(file_buf));
    json_raw(&w, "history", file_buf);

    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}
