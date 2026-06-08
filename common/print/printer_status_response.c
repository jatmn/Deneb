/* SPDX-License-Identifier: MPL-2.0 */
#include "printer_status_response.h"

#include "json_string.h"
#include "pending_job_file.h"
#include "print_profile.h"

#include <stdio.h>
#include <string.h>

void deneb_printer_status_response_init(
    deneb_printer_status_response_t *status)
{
    if (!status)
        return;
    memset(status, 0, sizeof(*status));
}

static const char *bool_text(int value)
{
    return value ? "true" : "false";
}

static void resolve_filename(const deneb_printer_status_response_t *status,
                             char *out,
                             size_t out_sz)
{
    char pending_name[128];

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    if (status && status->filename && status->filename[0])
        snprintf(out, out_sz, "%s", status->filename);
    else if (deneb_pending_job_file_default_display_name(
                 pending_name, sizeof(pending_name)) == 0 &&
             pending_name[0])
        snprintf(out, out_sz, "%s", pending_name);
}

static void format_nozzle_id(char *out, size_t out_sz)
{
    char raw[32];

    if (!out || out_sz == 0)
        return;
    deneb_print_profile_read_loaded_nozzle_id(raw, sizeof(raw));
    deneb_json_escape_string(raw, out, out_sz);
}

int deneb_printer_status_response_format_status(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    char safe_status[64];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(status->status_label ? status->status_label : "",
                             safe_status, sizeof(safe_status));
    n = snprintf(out, out_sz, "\"%s\"", safe_status);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_temperature(
    float current,
    float target,
    char *out,
    size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "{\"current\":%.1f,\"target\":%.1f}",
                 current, target);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_bed(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"temperature\":{\"current\":%.1f,\"target\":%.1f},"
                 "\"type\":\"glass\",\"pre_heat\":{\"active\":false}}",
                 status->bed_temp_cur, status->bed_temp_set);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_position(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f}",
                 status->pos_x, status->pos_y, status->pos_z);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_feeder(char *out, size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"acceleration\":3000.0,\"jerk\":5.0,"
                 "\"max_speed\":45.0}");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_material(char *out,
                                                     size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"GUID\":\"\",\"guid\":\"\",\"length_remaining\":-1}");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_led(char *out, size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"hue\":0.0,\"saturation\":0.0,\"brightness\":100.0}");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_led_brightness(char *out,
                                                           size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "100");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_led_hue(char *out, size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "0");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_led_saturation(char *out,
                                                           size_t out_sz)
{
    return deneb_printer_status_response_format_um_led_hue(out, out_sz);
}

int deneb_printer_status_response_format_um_ambient(char *out, size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "{\"current\":0.0}");
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_airmanager(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    const char *airmanager_status;
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    airmanager_status = status->topcap_present ? "connected" : "not_connected";
    n = snprintf(out, out_sz,
                 "{\"firmware_version\":\"\",\"filter_age\":0,"
                 "\"filter_max_age\":0,\"filter_status\":\"none\","
                 "\"status\":\"%s\",\"fan_speed\":0}",
                 airmanager_status);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_hotend(
    const deneb_printer_status_response_t *status,
    int include_offset,
    char *out,
    size_t out_sz)
{
    char id[64];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    format_nozzle_id(id, sizeof(id));
    if (include_offset) {
        n = snprintf(out, out_sz,
                     "{\"id\":\"%s\",\"serial\":\"\","
                     "\"temperature\":{\"current\":%.1f,\"target\":%.1f},"
                     "\"offset\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,"
                     "\"state\":\"valid\"}}",
                     id, status->nozzle_temp_cur, status->nozzle_temp_set);
    } else {
        n = snprintf(out, out_sz,
                     "{\"id\":\"%s\",\"serial\":\"\","
                     "\"temperature\":{\"current\":%.1f,\"target\":%.1f}}",
                     id, status->nozzle_temp_cur, status->nozzle_temp_set);
    }
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_extruder(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    char hotend[256];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;
    if (deneb_printer_status_response_format_um_hotend(
            status, 0, hotend, sizeof(hotend)) < 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"active_material\":{\"GUID\":\"\",\"guid\":\"\","
                 "\"length_remaining\":-1.0},"
                 "\"feeder\":{\"acceleration\":3000.0,\"jerk\":5.0,"
                 "\"max_speed\":45.0},"
                 "\"hotend\":%s}",
                 hotend);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_extruders(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    char extruder[512];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;
    if (deneb_printer_status_response_format_um_extruder(
            status, extruder, sizeof(extruder)) < 0)
        return -1;

    n = snprintf(out, out_sz, "[%s]", extruder);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_head(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"acceleration\":3000,"
                 "\"position\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f},"
                 "\"fan\":0,"
                 "\"jerk\":{\"x\":20.0,\"y\":20.0,\"z\":1.0},"
                 "\"max_speed\":{\"x\":300.0,\"y\":300.0,\"z\":40.0}}",
                 status->pos_x, status->pos_y, status->pos_z);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_heads(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    char head[512];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;
    if (deneb_printer_status_response_format_um_head(
            status, head, sizeof(head)) < 0)
        return -1;

    n = snprintf(out, out_sz, "[%s]", head);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_printer_status_response_format_um_root(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz)
{
    char filename_raw[128];
    char nozzle_id_buf[64];
    char filename[256];
    char status_label[64];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    resolve_filename(status, filename_raw, sizeof(filename_raw));
    format_nozzle_id(nozzle_id_buf, sizeof(nozzle_id_buf));
    deneb_json_escape_string(filename_raw, filename, sizeof(filename));
    deneb_json_escape_string(status->status_label ? status->status_label : "",
                             status_label, sizeof(status_label));

    n = snprintf(
        out, out_sz,
        "{"
        "\"bed\":{\"temperature\":{\"current\":%.1f,\"target\":%.1f},"
        "\"type\":\"glass\",\"pre_heat\":{\"active\":false}},"
        "\"heads\":[{\"acceleration\":3000,"
        "\"extruders\":[{\"active_material\":{\"GUID\":\"\",\"guid\":\"\","
        "\"length_remaining\":-1.0},"
        "\"feeder\":{\"acceleration\":3000.0,\"jerk\":5.0,"
        "\"max_speed\":45.0},"
        "\"hotend\":{\"id\":\"%s\",\"serial\":\"\","
        "\"temperature\":{\"current\":%.1f,\"target\":%.1f},"
        "\"offset\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"state\":\"valid\"},"
        "\"statistics\":{\"last_material_guid\":\"\","
        "\"material_extruded\":0,\"max_temperature_exposed\":0,"
        "\"time_spent_hot\":0}}}],"
        "\"position\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f},"
        "\"fan\":0,"
        "\"jerk\":{\"x\":20.0,\"y\":20.0,\"z\":1.0},"
        "\"max_speed\":{\"x\":300.0,\"y\":300.0,\"z\":40.0}}],"
        "\"status\":\"%s\",\"connected\":%s,\"is_printing\":%s,"
        "\"is_paused\":%s,\"has_error\":%s,\"progress\":%.1f,"
        "\"time_total\":%d,\"time_left\":%d,\"filename\":\"%s\","
        "\"diagnostics\":{}"
        "}",
        status->bed_temp_cur, status->bed_temp_set, nozzle_id_buf,
        status->nozzle_temp_cur, status->nozzle_temp_set, status->pos_x,
        status->pos_y, status->pos_z, status_label,
        bool_text(status->connected), bool_text(status->is_printing),
        bool_text(status->is_paused), bool_text(status->has_error),
        status->progress, status->time_total, status->time_left, filename);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
