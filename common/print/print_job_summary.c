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
    summary->state = deneb_print_job_state_or_none(has_error, is_paused,
                                                   is_printing);
    summary->active = deneb_print_job_is_active(has_error, is_paused,
                                                is_printing);
    summary->started = is_printing || is_paused;
    summary->time_total = time_total;
    summary->time_left = time_left;
    summary->time_elapsed = deneb_print_elapsed_seconds(time_total, time_left);
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
    char state[64];
    char uuid[128];
    char owner[80];
    char printer[96];
    char machine_family[96];
    char nozzle_raw[32];
    char material_raw[80];
    char nozzle_id[64];
    char material_guid[128];
    int n;

    if (!summary || !summary->active || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(created_at ? created_at : "", created,
                             sizeof(created));
    deneb_json_escape_string(DENEB_PRINT_PROFILE_MACHINE_VARIANT,
                             machine_variant, sizeof(machine_variant));
    deneb_json_escape_string(summary->name, name, sizeof(name));
    deneb_json_escape_string(summary->state, state, sizeof(state));
    deneb_json_escape_string(summary->uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(summary->source, owner, sizeof(owner));
    deneb_json_escape_string(printer_uuid ? printer_uuid : "", printer,
                             sizeof(printer));
    deneb_json_escape_string(DENEB_PRINT_PROFILE_MACHINE_FAMILY,
                             machine_family, sizeof(machine_family));
    deneb_print_profile_read_loaded_nozzle_id(nozzle_raw, sizeof(nozzle_raw));
    deneb_print_profile_read_loaded_material_guid(material_raw,
                                                  sizeof(material_raw));
    deneb_json_escape_string(nozzle_raw, nozzle_id, sizeof(nozzle_id));
    deneb_json_escape_string(material_raw, material_guid,
                             sizeof(material_guid));

    n = snprintf(
        out, out_sz,
        "[{\"created_at\":\"%s\",\"force\":false,"
        "\"machine_variant\":\"%s\",\"name\":\"%s\","
        "\"started\":%s,\"status\":\"%s\",\"time_total\":%d,"
        "\"time_elapsed\":%d,\"uuid\":\"%s\","
        "\"configuration\":[{\"extruder_index\":0,"
        "\"print_core_id\":\"%s\","
        "\"material\":{\"guid\":\"%s\",\"brand\":\"%s\",\"material\":\"%s\","
        "\"color\":\"%s\"}}],\"owner\":\"%s\","
        "\"printer_uuid\":\"%s\",\"assigned_to\":\"%s\","
        "\"build_plate\":{\"type\":\"glass\"},"
        "\"compatible_machine_families\":[\"%s\",\"%s\"],"
        "\"impediments_to_printing\":[]}]",
        created, machine_variant, name,
        summary->started ? "true" : "false", state, summary->time_total,
        summary->time_elapsed, uuid, nozzle_id, material_guid,
        DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND,
        DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE,
        DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_COLOR, owner, printer, printer,
        machine_family, machine_variant);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
