/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PENDING_JOB_FILE_H
#define DENEB_COMMON_PENDING_JOB_FILE_H

#include <stddef.h>

#define DENEB_PENDING_JOB_PATH "/tmp/deneb-cluster-print-job.json"

typedef struct {
    char path[1024];
    char name[128];
    char origin_name[96];
    char target_name[96];
    int tracker;
    int has_configuration_changes;
    int has_material_change;
    int has_print_core_change;
} deneb_pending_job_file_t;

void deneb_pending_job_file_init(deneb_pending_job_file_t *job);
int deneb_pending_job_file_load(const char *path,
                                deneb_pending_job_file_t *job);
int deneb_pending_job_file_load_default(deneb_pending_job_file_t *job);
int deneb_pending_job_file_has_conflict(const deneb_pending_job_file_t *job);
int deneb_pending_job_file_same_path(const char *pending_path,
                                     const char *candidate_path);
int deneb_pending_job_file_mark_handled(const char *path);
int deneb_pending_job_file_read_raw_array(const char *path,
                                          char *out,
                                          size_t out_sz,
                                          size_t *out_len);

#endif
