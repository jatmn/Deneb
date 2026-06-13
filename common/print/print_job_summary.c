/* SPDX-License-Identifier: MPL-2.0 */
#include "print_job_summary.h"

#include "json_string.h"
#include "print_profile.h"
#include "print_state_rules.h"

#include <stdio.h>

void deneb_print_job_summary_init(deneb_print_job_summary_t *summary,
                                  const char *name,
                                  const char *uuid,
                                  const char *source,
                                  int has_error,
                                  int is_paused,
                                  int is_printing,
                                  int time_total,
                                  int time_left,
                                  float progress_percent)
{
    if (!summary)
        return;

    summary->name = deneb_print_job_name_or_default(name);
    summary->uuid = deneb_print_job_uuid_or_default(uuid);
    summary->source = deneb_print_job_source_or_default(source);
    summary->cloud_job_id = "";
    summary->state = deneb_print_job_state_or_none(has_error, is_paused,
                                                   is_printing);
    summary->active = deneb_print_job_is_active(has_error, is_paused,
                                                is_printing);
    summary->started = is_printing || is_paused;
    summary->time_total = time_total;
    summary->time_left = time_left;
    summary->time_elapsed = deneb_print_elapsed_seconds(time_total, time_left);
    summary->created_at = 0;
    summary->progress_percent = progress_percent;
    summary->progress_fraction =
        deneb_print_progress_fraction(progress_percent);
}

void deneb_print_job_summary_init_queued(deneb_print_job_summary_t *summary,
                                         const char *name)
{
    if (!summary)
        return;

    deneb_print_job_summary_init(summary,
                                 name && name[0] ? name : "Print job",
                                 DENEB_PRINT_STOCK_API_JOB_UUID,
                                 DENEB_PRINT_WEB_API_JOB_SOURCE,
                                 0, 0, 1, 0, 0, 0.0f);
    summary->state = DENEB_PRINT_PHASE_NAME_PRE_PRINT;
    summary->active = 1;
    summary->started = 0;
    summary->time_elapsed = 0;
    summary->progress_fraction = 0.0f;
    summary->progress_percent = 0.0f;
}

int deneb_print_job_summary_format_queued_response(const char *message,
                                                   const char *name,
                                                   char *out,
                                                   size_t out_sz)
{
    deneb_print_job_summary_t summary;
    char msg[192];
    char safe_name[256];
    char uuid[128];
    char source[80];
    char state[64];
    int n;

    if (!out || out_sz == 0)
        return -1;

    deneb_print_job_summary_init_queued(&summary, name);
    deneb_json_escape_string(message ? message : "", msg, sizeof(msg));
    deneb_json_escape_string(summary.name, safe_name, sizeof(safe_name));
    deneb_json_escape_string(summary.uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(summary.source, source, sizeof(source));
    deneb_json_escape_string(summary.state, state, sizeof(state));

    n = snprintf(out, (size_t)out_sz,
                 "{\"message\":\"%s\",\"name\":\"%s\",\"uuid\":\"%s\","
                 "\"source\":\"%s\",\"state\":\"%s\",\"progress\":%.1f,"
                 "\"time_elapsed\":%d,\"time_total\":%d}",
                 msg, safe_name, uuid, source, state,
                 summary.progress_fraction, summary.time_elapsed,
                 summary.time_total);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_um_response(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz)
{
    char name[256];
    char uuid[128];
    char source[80];
    char state[64];
    int n;

    if (!summary || !summary->active || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(summary->name, name, sizeof(name));
    deneb_json_escape_string(summary->uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(summary->source, source, sizeof(source));
    deneb_json_escape_string(summary->state, state, sizeof(state));

    n = snprintf(out, out_sz,
                 "{\"name\":\"%s\",\"uuid\":\"%s\",\"source\":\"%s\","
                 "\"state\":\"%s\",\"progress\":%.1f,"
                 "\"time_elapsed\":%d,\"time_total\":%d,"
                 "\"datetime_started\":\"\",\"datetime_finished\":\"\"}",
                 name, uuid, source, state, summary->progress_fraction,
                 summary->time_elapsed, summary->time_total);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_deneb_current_response(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz)
{
    char name[256];
    char uuid[128];
    char source[80];
    char state[64];
    int n;

    if (!summary || !summary->active || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(summary->name, name, sizeof(name));
    deneb_json_escape_string(summary->uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(summary->source, source, sizeof(source));
    deneb_json_escape_string(summary->state, state, sizeof(state));

    n = snprintf(out, out_sz,
                 "{\"name\":\"%s\",\"uuid\":\"%s\",\"source\":\"%s\","
                 "\"state\":\"%s\",\"progress\":%.1f,\"time_total\":%d,"
                 "\"time_elapsed\":%d,\"time_left\":%d}",
                 name, uuid, source, state, summary->progress_percent,
                 summary->time_total, summary->time_elapsed,
                 summary->time_left);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_string_field(
    const deneb_print_job_summary_t *summary,
    deneb_print_job_summary_string_field_t field,
    char *out,
    size_t out_sz)
{
    char value[256];
    const char *raw = "";
    int n;

    if (!summary || !out || out_sz == 0)
        return -1;

    switch (field) {
    case DENEB_PRINT_JOB_SUMMARY_FIELD_NAME:
        raw = summary->name;
        break;
    case DENEB_PRINT_JOB_SUMMARY_FIELD_UUID:
        raw = summary->uuid;
        break;
    case DENEB_PRINT_JOB_SUMMARY_FIELD_SOURCE:
        raw = summary->source;
        break;
    case DENEB_PRINT_JOB_SUMMARY_FIELD_STATE:
        raw = summary->state;
        break;
    case DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_STARTED:
    case DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_FINISHED:
        raw = "";
        break;
    default:
        return -1;
    }

    deneb_json_escape_string(raw, value, sizeof(value));
    n = snprintf(out, out_sz, "\"%s\"", value);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_progress_fraction(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz)
{
    int n;

    if (!summary || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "%.1f", summary->progress_fraction);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_time_elapsed(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz)
{
    int n;

    if (!summary || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "%d", summary->time_elapsed);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_time_total(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz)
{
    int n;

    if (!summary || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz, "%d", summary->time_total);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_print_job_summary_format_cluster_active_response(
    const deneb_print_job_summary_t *summary,
    const char *printer_uuid,
    const char *created_at,
    char *out,
    size_t out_sz)
{
    char created[64];
    char machine_variant[96];
    char name[256];
    char state_raw[64];
    char state[64];
    char uuid[128];
    char cloud_job_id[192];
    char cloud_job_id_binding[224];
    char printer[96];
    char nozzle_raw[32];
    char material_raw[80];
    char nozzle_id[64];
    char material_guid[128];
    char material_type_raw[64];
    char material_color_raw[64];
    char material_type[128];
    char material_color[128];
    int n;

    if (!summary || !summary->active || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(created_at ? created_at : "", created,
                             sizeof(created));
    deneb_json_escape_string(DENEB_PRINT_PROFILE_MACHINE_VARIANT,
                             machine_variant, sizeof(machine_variant));
    deneb_json_escape_string(summary->name, name, sizeof(name));
    snprintf(state_raw, sizeof(state_raw), "%s",
             deneb_print_cluster_job_status_label(summary->state));
    deneb_json_escape_string(state_raw, state, sizeof(state));
    deneb_json_escape_string(deneb_print_cluster_job_uuid_or_default(summary->uuid),
                             uuid, sizeof(uuid));
    deneb_json_escape_string(summary->cloud_job_id ? summary->cloud_job_id : "",
                             cloud_job_id, sizeof(cloud_job_id));
    if (cloud_job_id[0])
        snprintf(cloud_job_id_binding, sizeof(cloud_job_id_binding),
                 "\"cloud_job_id\":\"%s\",", cloud_job_id);
    else
        cloud_job_id_binding[0] = '\0';
    deneb_json_escape_string(printer_uuid ? printer_uuid : "", printer,
                             sizeof(printer));
    deneb_print_profile_read_loaded_nozzle_id(nozzle_raw, sizeof(nozzle_raw));
    deneb_print_profile_read_loaded_cluster_material_guid(material_raw,
                                                          sizeof(material_raw));
    deneb_print_profile_material_type_from_guid(material_raw,
                                                material_type_raw,
                                                sizeof(material_type_raw));
    deneb_print_profile_material_color_from_guid(material_raw,
                                                 material_color_raw,
                                                 sizeof(material_color_raw));
    deneb_json_escape_string(nozzle_raw, nozzle_id, sizeof(nozzle_id));
    deneb_json_escape_string(material_raw, material_guid,
                             sizeof(material_guid));
    deneb_json_escape_string(material_type_raw, material_type,
                             sizeof(material_type));
    deneb_json_escape_string(material_color_raw, material_color,
                             sizeof(material_color));

    if (material_guid[0]) {
        char with_guid[4096];
        n = snprintf(
            with_guid, sizeof(with_guid),
            "[{\"created_at\":\"%s\",\"force\":false,"
            "\"machine_variant\":\"%s\",\"name\":\"%s\","
            "\"started\":%s,\"status\":\"%s\",\"time_total\":%d,"
            "\"time_elapsed\":%d,\"uuid\":\"%s\",%s"
            "\"configuration\":[{\"extruder_index\":0,"
            "\"print_core_id\":\"%s\","
            "\"material\":{\"guid\":\"%s\",\"brand\":\"%s\",\"material\":\"%s\","
            "\"color\":\"%s\"}}],"
            "\"printer_uuid\":\"%s\",\"assigned_to\":\"%s\","
            "\"constraints\":{}}]",
            created, machine_variant, name,
            summary->started ? "true" : "false", state, summary->time_total,
            summary->time_elapsed, uuid, cloud_job_id_binding, nozzle_id, material_guid,
            DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND, material_type,
            material_color, printer, printer);
        if (n < 0 || (size_t)n >= sizeof(with_guid) ||
            (size_t)n >= out_sz)
            return -1;
        snprintf(out, out_sz, "%s", with_guid);
        return n;
    }
    n = snprintf(
        out, out_sz,
        "[{\"created_at\":\"%s\",\"force\":false,"
        "\"machine_variant\":\"%s\",\"name\":\"%s\","
        "\"started\":%s,\"status\":\"%s\",\"time_total\":%d,"
        "\"time_elapsed\":%d,\"uuid\":\"%s\",%s"
        "\"configuration\":[{\"extruder_index\":0,"
        "\"print_core_id\":\"%s\","
        "\"material\":{\"brand\":\"%s\",\"material\":\"%s\","
        "\"color\":\"%s\"}}],"
        "\"printer_uuid\":\"%s\",\"assigned_to\":\"%s\","
        "\"constraints\":{}}]",
        created, machine_variant, name,
        summary->started ? "true" : "false", state, summary->time_total,
        summary->time_elapsed, uuid, cloud_job_id_binding, nozzle_id,
        DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND, material_type,
        material_color, printer, printer);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
