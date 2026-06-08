/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * System endpoint implementations. Reads from /proc, UCI, and static values.
 */

#include "api_system.h"
#include "json_writer.h"
#include "printer_identity.h"
#include "system_language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void api_system_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char hostname[64], guid[48];
    char lang[16];
    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    deneb_printer_identity_guid(guid, sizeof(guid));
    deneb_system_language_read(lang, sizeof(lang));

    char buf[768];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "name", hostname);
    json_str(&w, "hostname", hostname);
    json_key(&w, "firmware");
    json_obj_open(&w);
    json_str(&w, "version", DENEB_VERSION);
    json_obj_close(&w);
    json_key(&w, "hardware");
    json_obj_open(&w);
    json_int(&w, "typeid", 9065);  /* UM2+ Connect */
    json_int(&w, "revision", 0);
    json_obj_close(&w);
    json_str(&w, "guid", guid);
    json_str(&w, "platform", "mipsel");
    json_str(&w, "type", "Ultimaker 2+ Connect");
    json_str(&w, "variant", "Deneb");
    json_str(&w, "language", lang);
    json_str(&w, "country", "US");
    json_bool(&w, "is_country_locked", 0);
    json_key(&w, "time");
    json_obj_open(&w);
    json_int(&w, "utc", (long long)time(NULL));
    json_obj_close(&w);

    /* Read memory from /proc/meminfo */
    long mem_total = 0, mem_free = 0;
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1) continue;
            if (sscanf(line, "MemAvailable: %ld kB", &mem_free) == 1) continue;
        }
        fclose(mf);
    }
    json_key(&w, "memory");
    json_obj_open(&w);
    json_int(&w, "total", mem_total);
    json_int(&w, "used", mem_total - mem_free);
    json_obj_close(&w);

    /* Read uptime from /proc/uptime */
    double uptime = 0;
    FILE *uf = fopen("/proc/uptime", "r");
    if (uf) { fscanf(uf, "%lf", &uptime); fclose(uf); }
    json_int(&w, "uptime", (long long)uptime);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_system_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char hostname[64];
    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    char buf[96];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, hostname);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_system_hostname_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char hostname[64];
    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    char buf[96];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, hostname);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_system_firmware_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[64];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, DENEB_VERSION);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_system_hardware_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "{\"typeid\":9065,\"revision\":0}");
}

void api_system_type_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"Ultimaker 2+ Connect\"");
}

void api_system_variant_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"Deneb\"");
}

void api_system_platform_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"mipsel\"");
}

void api_system_memory_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    long total = 0, free_val = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
            if (sscanf(line, "MemAvailable: %ld kB", &free_val) == 1) continue;
        }
        fclose(f);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"total\":%ld,\"used\":%ld}", total, total - free_val);
    api_http_set_body_str(resp, buf);
}

void api_system_uptime_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    double uptime = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        fscanf(f, "%lf", &uptime);
        fclose(f);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)uptime);
    api_http_set_body_str(resp, buf);
}

void api_system_language_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char lang[16] = "en";
    deneb_system_language_read(lang, sizeof(lang));
    char buf[32];
    snprintf(buf, sizeof(buf), "\"%s\"", lang);
    api_http_set_body_str(resp, buf);
}

void api_system_country_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"US\"");
}

void api_system_time_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"utc\":%lld}", (long long)time(NULL));
    api_http_set_body_str(resp, buf);
}

void api_system_guid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    char guid[48];
    deneb_printer_identity_guid(guid, sizeof(guid));
    char buf[64];
    snprintf(buf, sizeof(buf), "\"%s\"", guid);
    api_http_set_body_str(resp, buf);
}

void api_system_log_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    /* Read last 20 lines of system log */
    char buf[8192];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_arr_open(&w);

    FILE *f = popen("logread 2>/dev/null | tail -20", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            json_arr_str(&w, line);
        }
        pclose(f);
    }

    json_arr_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}
