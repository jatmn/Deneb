/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job.h"
#include "pending_job_file.h"
#include "json_string.h"
#include "print_control.h"
#include "print_profile.h"
#include "printer_identity.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void deneb_pending_job_init(deneb_pending_job_t *job, const char *path)
{
    char display_name[128];

    if (!job)
        return;

    memset(job, 0, sizeof(*job));
    snprintf(job->path, sizeof(job->path), "%s", path ? path : "");
    if (deneb_pending_job_file_display_value(path, display_name, sizeof(display_name)) != 0)
        snprintf(display_name, sizeof(display_name), "%s", "Print job");
    snprintf(job->name, sizeof(job->name), "%s", display_name);
    snprintf(job->uuid, sizeof(job->uuid), "%s", DENEB_PRINT_DEFAULT_JOB_UUID);
    snprintf(job->source, sizeof(job->source), "%s", DENEB_PRINT_DEFAULT_JOB_SOURCE);
    snprintf(job->owner, sizeof(job->owner), "%s", DENEB_PRINT_DEFAULT_JOB_SOURCE);
    snprintf(job->machine_variant, sizeof(job->machine_variant), "%s", DENEB_PRINT_PROFILE_MACHINE_VARIANT);
    snprintf(job->machine_family, sizeof(job->machine_family), "%s", DENEB_PRINT_PROFILE_MACHINE_FAMILY);
    snprintf(job->material_guid, sizeof(job->material_guid), "%s", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID);
    snprintf(job->origin_material_guid, sizeof(job->origin_material_guid), "%s", "");
    snprintf(job->origin_material_name, sizeof(job->origin_material_name), "%s", "");
    snprintf(job->target_material_name, sizeof(job->target_material_name), "%s", "");
    snprintf(job->material_brand, sizeof(job->material_brand), "%s", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND);
    snprintf(job->material_type, sizeof(job->material_type), "%s", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE);
    snprintf(job->material_color, sizeof(job->material_color), "%s", DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_COLOR);
    snprintf(job->nozzle_id, sizeof(job->nozzle_id), "%s", DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_ID);
    snprintf(job->origin_nozzle_id, sizeof(job->origin_nozzle_id), "%s", "");
    job->tracker = -1;
}

int deneb_pending_job_change_count(const deneb_pending_job_t *job)
{
    int count = 0;

    if (!job)
        return 0;
    if (job->material_change_required)
        count++;
    if (job->print_core_change_required)
        count++;
    return count;
}

static void format_pending_created_at(const deneb_pending_job_t *job,
                                      char *out,
                                      size_t out_sz)
{
    time_t t;
    struct tm tm_utc;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';
    t = job && job->tracker >= 0 ? (time_t)job->tracker : time(NULL);
    if (!gmtime_r(&t, &tm_utc))
        return;
    strftime(out, out_sz, "%Y-%m-%dT%H:%M:%S.000Z", &tm_utc);
}

int deneb_pending_job_serialize(const deneb_pending_job_t *job,
                                char *out, size_t out_sz)
{
    char created_at[40];
    char path[512];
    char name[256];
    char uuid[128];
    char printer_uuid_raw[64];
    char printer_uuid[128];
    char cloud_job_id[192];
    char cloud_job_id_binding[224];
    char owner[80];
    char variant[128];
    char family[128];
    char guid[128];
    char target_guid[128];
    char material_guid_binding[160];
    char origin_guid[128];
    char origin_material_name[128];
    char target_material_name[128];
    char brand[80];
    char material[80];
    char color[40];
    char nozzle[48];
    char origin_nozzle[48];
    char changes[1024] = "";
    char printer_binding[192];
    const char *status;
    const char *started;
    int has_changes;
    int n;

    if (!job || !out || out_sz == 0 || !job->path[0])
        return -1;

    format_pending_created_at(job, created_at, sizeof(created_at));
    deneb_printer_identity_guid(printer_uuid_raw, sizeof(printer_uuid_raw));
    deneb_json_escape_string(printer_uuid_raw, printer_uuid,
                             sizeof(printer_uuid));
    deneb_json_escape_string(job->path, path, sizeof(path));
    deneb_json_escape_string(job->name, name, sizeof(name));
    deneb_json_escape_string(deneb_print_cluster_job_uuid_or_default(job->uuid),
                             uuid, sizeof(uuid));
    deneb_json_escape_string(job->cloud_job_id, cloud_job_id,
                             sizeof(cloud_job_id));
    if (cloud_job_id[0])
        snprintf(cloud_job_id_binding, sizeof(cloud_job_id_binding),
                 "\"cloud_job_id\":\"%s\",", cloud_job_id);
    else
        cloud_job_id_binding[0] = '\0';
    deneb_json_escape_string(job->owner, owner, sizeof(owner));
    deneb_json_escape_string(job->machine_variant, variant, sizeof(variant));
    deneb_json_escape_string(job->machine_family, family, sizeof(family));
    if (deneb_print_is_cluster_guid(job->material_guid))
        deneb_json_escape_string(job->material_guid, guid, sizeof(guid));
    else
        guid[0] = '\0';
    deneb_json_escape_string(job->material_guid, target_guid,
                             sizeof(target_guid));
    deneb_json_escape_string(job->origin_material_guid, origin_guid, sizeof(origin_guid));
    deneb_json_escape_string(job->origin_material_name, origin_material_name, sizeof(origin_material_name));
    deneb_json_escape_string(job->target_material_name, target_material_name, sizeof(target_material_name));
    deneb_json_escape_string(job->material_brand, brand, sizeof(brand));
    deneb_json_escape_string(job->material_type, material, sizeof(material));
    deneb_json_escape_string(job->material_color, color, sizeof(color));
    deneb_json_escape_string(job->nozzle_id, nozzle, sizeof(nozzle));
    deneb_json_escape_string(job->origin_nozzle_id, origin_nozzle, sizeof(origin_nozzle));

    if (job->material_change_required) {
        snprintf(changes + strlen(changes), sizeof(changes) - strlen(changes),
                 "{\"type_of_change\":\"material_change\",\"index\":0,"
                 "\"origin_id\":\"%s\",\"origin_name\":\"%s\",\"target_id\":\"%s\","
                 "\"target_name\":\"%s\"}",
                 origin_guid,
                 origin_material_name[0] ? origin_material_name : origin_guid,
                 target_guid,
                 target_material_name[0] ? target_material_name : material);
    }
    if (job->print_core_change_required) {
        snprintf(changes + strlen(changes), sizeof(changes) - strlen(changes),
                 "%s{\"type_of_change\":\"print_core_change\",\"index\":0,"
                 "\"origin_id\":\"%s\",\"origin_name\":\"%s\",\"target_id\":\"%s\","
                 "\"target_name\":\"%s\"}",
                 changes[0] ? "," : "",
                 origin_nozzle[0] ? origin_nozzle : "",
                 origin_nozzle[0] ? origin_nozzle : "",
                 nozzle,
                 nozzle);
    }

    has_changes = deneb_pending_job_change_count(job) > 0;
    status = has_changes ? "wait_user_action" :
        deneb_print_control_phase_name(DENEB_PRINT_PHASE_PREPARING);
    started = has_changes ? "false" : "true";
    if (has_changes)
        printer_binding[0] = '\0';
    else
        snprintf(printer_binding, sizeof(printer_binding),
                 "\"printer_uuid\":\"%s\",", printer_uuid);
    if (guid[0])
        snprintf(material_guid_binding, sizeof(material_guid_binding),
                 "\"guid\":\"%s\",", guid);
    else
        material_guid_binding[0] = '\0';

    n = snprintf(out, out_sz,
                 "[{\"uuid\":\"%s\",\"created_at\":\"%s\",\"name\":\"%s\","
                 "\"path\":\"%s\",%s"
                 "\"status\":\"%s\",\"time_total\":0,"
                 "\"time_elapsed\":0,\"started\":%s,\"force\":false,"
                 "\"machine_variant\":\"%s\",\"owner\":\"%s\","
                 "%s\"assigned_to\":\"%s\","
                 "\"build_plate\":{\"type\":\"glass\"},"
                 "\"configuration\":[{\"extruder_index\":0,"
                 "\"print_core_id\":\"%s\",\"material\":{%s"
                 "\"brand\":\"%s\",\"material\":\"%s\",\"color\":\"%s\"}}],"
                 "\"constraints\":{},"
                 "\"compatible_machine_families\":[\"%s\",\"%s\"],"
                 "\"impediments_to_printing\":[]",
                 uuid, created_at, name, path, cloud_job_id_binding, status, started,
                 variant, owner, printer_binding,
                 printer_uuid, nozzle, material_guid_binding, brand, material,
                 color, family, variant);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;

    if (job->tracker >= 0) {
        int m = snprintf(out + n, out_sz - (size_t)n,
                         ",\"deneb_tracker\":%d", job->tracker);
        if (m < 0 || (size_t)m >= out_sz - (size_t)n)
            return -1;
        n += m;
    }

    if (changes[0]) {
        int m = snprintf(out + n, out_sz - (size_t)n,
                         ",\"configuration_changes_required\":[%s]", changes);
        if (m < 0 || (size_t)m >= out_sz - (size_t)n)
            return -1;
        n += m;
    }

    if ((size_t)n + 3 >= out_sz)
        return -1;
    snprintf(out + n, out_sz - (size_t)n, "}]\n");
    return (int)strlen(out);
}

int deneb_pending_job_write_file(const deneb_pending_job_t *job,
                                 const char *path)
{
    char json[4096];
    char tmp_path[320];
    FILE *f;
    int len;
    int n;

    if (!job || !path || !path[0])
        return -1;

    len = deneb_pending_job_serialize(job, json, sizeof(json));
    if (len < 0)
        return -1;

    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path))
        return -1;

    f = fopen(tmp_path, "wb");
    if (!f)
        return -1;
    if (fwrite(json, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp_path);
        return -1;
    }
    if (rename(tmp_path, path) < 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}

int deneb_pending_job_write_default(const deneb_pending_job_t *job)
{
    return deneb_pending_job_write_file(job, DENEB_PENDING_JOB_PATH);
}
