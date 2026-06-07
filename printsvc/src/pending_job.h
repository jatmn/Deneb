/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_PENDING_JOB_H
#define DENEB_PRINTSVC_PENDING_JOB_H

#include <stddef.h>

typedef struct {
    char path[256];
    char name[128];
    char uuid[64];
    char source[32];
    char owner[32];
    char machine_variant[64];
    char machine_family[64];
    char material_guid[64];
    char material_brand[32];
    char material_type[32];
    char material_color[16];
    char nozzle_id[24];
    int tracker;
    int material_change_required;
    int print_core_change_required;
} deneb_pending_job_t;

void deneb_pending_job_init(deneb_pending_job_t *job, const char *path);
int deneb_pending_job_change_count(const deneb_pending_job_t *job);
int deneb_pending_job_serialize(const deneb_pending_job_t *job,
                                char *out, size_t out_sz);

#endif
