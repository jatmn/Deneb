/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job.h"
#include "print_control.h"

#include <stdio.h>
#include <string.h>

#define DENEB_PENDING_DEFAULT_UUID "deneb-current-job"
#define DENEB_PENDING_DEFAULT_SOURCE "WEB_API"
#define DENEB_PENDING_DEFAULT_OWNER "Cura"
#define DENEB_PENDING_DEFAULT_VARIANT "Ultimaker 2+ Connect"
#define DENEB_PENDING_DEFAULT_FAMILY "ultimaker2_plus_connect"
#define DENEB_PENDING_DEFAULT_GUID "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"
#define DENEB_PENDING_DEFAULT_BRAND "Generic"
#define DENEB_PENDING_DEFAULT_TYPE "PLA"
#define DENEB_PENDING_DEFAULT_COLOR "#ffc924"
#define DENEB_PENDING_DEFAULT_NOZZLE "0.4 mm"

static const char *base_name(const char *path)
{
    const char *slash;
    const char *backslash;

    if (!path || !*path)
        return "Print job";

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    return slash ? slash + 1 : path;
}

static void json_escape(const char *src, char *out, size_t out_sz)
{
    size_t oi = 0;

    if (!out || out_sz == 0)
        return;

    for (size_t i = 0; src && src[i] && oi + 1 < out_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c == '"' || c == '\\') && oi + 2 < out_sz) {
            out[oi++] = '\\';
            out[oi++] = (char)c;
        } else if (c >= 0x20) {
            out[oi++] = (char)c;
        }
    }
    out[oi] = '\0';
}

void deneb_pending_job_init(deneb_pending_job_t *job, const char *path)
{
    if (!job)
        return;

    memset(job, 0, sizeof(*job));
    snprintf(job->path, sizeof(job->path), "%s", path ? path : "");
    snprintf(job->name, sizeof(job->name), "%s", base_name(path));
    snprintf(job->uuid, sizeof(job->uuid), "%s", DENEB_PENDING_DEFAULT_UUID);
    snprintf(job->source, sizeof(job->source), "%s", DENEB_PENDING_DEFAULT_SOURCE);
    snprintf(job->owner, sizeof(job->owner), "%s", DENEB_PENDING_DEFAULT_OWNER);
    snprintf(job->machine_variant, sizeof(job->machine_variant), "%s", DENEB_PENDING_DEFAULT_VARIANT);
    snprintf(job->machine_family, sizeof(job->machine_family), "%s", DENEB_PENDING_DEFAULT_FAMILY);
    snprintf(job->material_guid, sizeof(job->material_guid), "%s", DENEB_PENDING_DEFAULT_GUID);
    snprintf(job->material_brand, sizeof(job->material_brand), "%s", DENEB_PENDING_DEFAULT_BRAND);
    snprintf(job->material_type, sizeof(job->material_type), "%s", DENEB_PENDING_DEFAULT_TYPE);
    snprintf(job->material_color, sizeof(job->material_color), "%s", DENEB_PENDING_DEFAULT_COLOR);
    snprintf(job->nozzle_id, sizeof(job->nozzle_id), "%s", DENEB_PENDING_DEFAULT_NOZZLE);
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

int deneb_pending_job_serialize(const deneb_pending_job_t *job,
                                char *out, size_t out_sz)
{
    char path[512];
    char name[256];
    char uuid[128];
    char source[80];
    char owner[80];
    char variant[128];
    char family[128];
    char guid[128];
    char brand[80];
    char material[80];
    char color[40];
    char nozzle[48];
    char changes[1024] = "";
    const char *status;
    int n;

    if (!job || !out || out_sz == 0 || !job->path[0])
        return -1;

    json_escape(job->path, path, sizeof(path));
    json_escape(job->name, name, sizeof(name));
    json_escape(job->uuid, uuid, sizeof(uuid));
    json_escape(job->source, source, sizeof(source));
    json_escape(job->owner, owner, sizeof(owner));
    json_escape(job->machine_variant, variant, sizeof(variant));
    json_escape(job->machine_family, family, sizeof(family));
    json_escape(job->material_guid, guid, sizeof(guid));
    json_escape(job->material_brand, brand, sizeof(brand));
    json_escape(job->material_type, material, sizeof(material));
    json_escape(job->material_color, color, sizeof(color));
    json_escape(job->nozzle_id, nozzle, sizeof(nozzle));

    if (job->material_change_required) {
        snprintf(changes + strlen(changes), sizeof(changes) - strlen(changes),
                 "{\"type_of_change\":\"material_change\",\"index\":0,"
                 "\"origin_id\":\"\",\"origin_name\":\"\",\"target_id\":\"%s\","
                 "\"target_name\":\"%s\"}", guid, material);
    }
    if (job->print_core_change_required) {
        snprintf(changes + strlen(changes), sizeof(changes) - strlen(changes),
                 "%s{\"type_of_change\":\"print_core_change\",\"index\":0,"
                 "\"origin_id\":\"\",\"origin_name\":\"\",\"target_id\":\"%s\","
                 "\"target_name\":\"%s\"}",
                 changes[0] ? "," : "", nozzle, nozzle);
    }

    status = deneb_pending_job_change_count(job) > 0 ?
        "wait_user_action" :
        deneb_print_control_phase_name(DENEB_PRINT_PHASE_PREPARING);

    n = snprintf(out, out_sz,
                 "[{\"uuid\":\"%s\",\"created_at\":\"\",\"name\":\"%s\","
                 "\"path\":\"%s\",\"status\":\"%s\",\"time_total\":0,"
                 "\"time_elapsed\":0,\"started\":true,\"force\":false,"
                 "\"machine_variant\":\"%s\",\"owner\":\"%s\","
                 "\"assigned_to\":\"00000000-0000-0000-0000-000000000000\","
                 "\"build_plate\":{\"type\":\"glass\"},"
                 "\"configuration\":[{\"extruder_index\":0,"
                 "\"print_core_id\":\"%s\",\"material\":{\"guid\":\"%s\","
                 "\"brand\":\"%s\",\"material\":\"%s\",\"color\":\"%s\"}}],"
                 "\"compatible_machine_families\":[\"%s\",\"%s\"],"
                 "\"impediments_to_printing\":[]",
                 uuid, name, path, status, variant, owner, nozzle, guid,
                 brand, material, color, family, variant);
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
