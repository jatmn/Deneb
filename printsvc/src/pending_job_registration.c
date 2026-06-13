/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job_registration.h"

#include "print_job_file.h"
#include "print_profile.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>

void deneb_pending_job_registration_init(
    deneb_pending_job_registration_t *registration)
{
    if (!registration)
        return;
    memset(registration, 0, sizeof(*registration));
}

int deneb_pending_job_registration_prepare(
    const char *path,
    const char *source,
    const char *uuid,
    const char *cloud_job_id,
    long long tracker_seed,
    deneb_pending_job_registration_t *registration)
{
    char loaded_guid[64];
    char loaded_nozzle[24];
    char target_guid[64];
    char target_nozzle_raw[24];
    char target_nozzle[24];
    char loaded_name[64];
    char target_name[64];
    deneb_print_job_file_metadata_t meta;

    if (!path || !*path || !registration)
        return -1;

    deneb_pending_job_registration_init(registration);

    deneb_print_profile_read_loaded_material_guid(loaded_guid,
                                                  sizeof(loaded_guid));
    deneb_print_profile_read_loaded_nozzle_size(loaded_nozzle,
                                                sizeof(loaded_nozzle));

    snprintf(target_guid, sizeof(target_guid), "%s", loaded_guid);
    snprintf(target_nozzle_raw, sizeof(target_nozzle_raw), "%s",
             loaded_nozzle);
    deneb_print_job_file_metadata_init(&meta);
    if (deneb_print_job_file_metadata_load(path, &meta) == 0) {
        if (meta.material_guid[0])
            snprintf(target_guid, sizeof(target_guid), "%s",
                     meta.material_guid);
        if (meta.nozzle_size[0])
            snprintf(target_nozzle_raw, sizeof(target_nozzle_raw), "%s",
                     meta.nozzle_size);
    }

    deneb_print_profile_normalize_nozzle_id(loaded_nozzle, loaded_nozzle,
                                            sizeof(loaded_nozzle));
    deneb_print_profile_normalize_nozzle_id(target_nozzle_raw, target_nozzle,
                                            sizeof(target_nozzle));
    deneb_print_profile_material_name_from_guid(loaded_guid, loaded_name,
                                                sizeof(loaded_name));
    deneb_print_profile_material_name_from_guid(target_guid, target_name,
                                                sizeof(target_name));

    deneb_pending_job_init(&registration->job, path);
    registration->job.tracker = (int)(tracker_seed & 0x7fffffff);
    snprintf(registration->job.source, sizeof(registration->job.source), "%s",
             deneb_print_job_source_or_default(source));
    snprintf(registration->job.uuid, sizeof(registration->job.uuid), "%s",
             deneb_print_job_uuid_or_default(uuid));
    snprintf(registration->job.cloud_job_id,
             sizeof(registration->job.cloud_job_id), "%s",
             cloud_job_id ? cloud_job_id : "");
    snprintf(registration->job.material_guid,
             sizeof(registration->job.material_guid), "%s", target_guid);
    snprintf(registration->job.origin_material_guid,
             sizeof(registration->job.origin_material_guid), "%s",
             loaded_guid);
    snprintf(registration->job.origin_material_name,
             sizeof(registration->job.origin_material_name), "%s",
             loaded_name);
    snprintf(registration->job.target_material_name,
             sizeof(registration->job.target_material_name), "%s",
             target_name);
    snprintf(registration->job.material_type,
             sizeof(registration->job.material_type), "%s", target_name);
    snprintf(registration->job.nozzle_id,
             sizeof(registration->job.nozzle_id), "%s", target_nozzle);
    snprintf(registration->job.origin_nozzle_id,
             sizeof(registration->job.origin_nozzle_id), "%s",
             loaded_nozzle);

    registration->job.material_change_required =
        loaded_guid[0] && target_guid[0] &&
        strcmp(loaded_guid, target_guid) != 0;
    registration->job.print_core_change_required =
        loaded_nozzle[0] && target_nozzle[0] &&
        strcmp(loaded_nozzle, target_nozzle) != 0;

    registration->change_count =
        deneb_pending_job_change_count(&registration->job);
    registration->should_start_immediately =
        registration->change_count == 0;
    return 0;
}

int deneb_pending_job_registration_write_default(
    const deneb_pending_job_registration_t *registration)
{
    if (!registration)
        return -1;
    return deneb_pending_job_write_default(&registration->job);
}

int deneb_pending_job_registration_dispatch_start(
    const deneb_pending_job_registration_t *registration,
    const deneb_pending_job_registration_dispatch_ops_t *ops)
{
    deneb_print_job_start_plan_t plan;

    if (!registration)
        return -1;

    if (!registration->should_start_immediately)
        return 0;

    if (!ops || !ops->start_allowed || !ops->send_job)
        return -1;

    if (!ops->start_allowed(ops->ctx))
        return -1;

    if (deneb_print_job_start_plan_prepare(
            registration->job.path,
            registration->job.source,
            registration->job.uuid,
            registration->job.cloud_job_id,
            0.0f,
            0.0f,
            &plan) < 0)
        return -1;

    return ops->send_job(ops->ctx, &plan);
}
