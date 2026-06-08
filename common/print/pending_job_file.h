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

typedef enum {
    DENEB_PENDING_JOB_ACTION_NONE = 0,
    DENEB_PENDING_JOB_ACTION_START,
    DENEB_PENDING_JOB_ACTION_ABORT
} deneb_pending_job_action_kind_t;

typedef struct {
    deneb_pending_job_action_kind_t kind;
    char command[16];
    char path[1024];
    char source[32];
    char uuid[64];
    int tracker;
    int mark_handled_after_success;
    int clear_after_success;
} deneb_pending_job_action_plan_t;

typedef enum {
    DENEB_PENDING_JOB_UPLOAD_CLEAR = 0,
    DENEB_PENDING_JOB_UPLOAD_DUPLICATE,
    DENEB_PENDING_JOB_UPLOAD_BLOCKED
} deneb_pending_job_upload_status_t;

typedef struct {
    deneb_pending_job_upload_status_t status;
    char path[1024];
    char display_name[128];
    int tracker;
} deneb_pending_job_upload_check_t;

void deneb_pending_job_file_init(deneb_pending_job_file_t *job);
void deneb_pending_job_action_plan_init(deneb_pending_job_action_plan_t *plan);
void deneb_pending_job_upload_check_init(deneb_pending_job_upload_check_t *check);
int deneb_pending_job_file_load(const char *path,
                                deneb_pending_job_file_t *job);
int deneb_pending_job_file_load_default(deneb_pending_job_file_t *job);
int deneb_pending_job_file_load_pending_default(deneb_pending_job_file_t *job);
int deneb_pending_job_file_load_conflict_default(deneb_pending_job_file_t *job);
int deneb_pending_job_file_plan_action(const deneb_pending_job_file_t *job,
                                       const char *instruction,
                                       deneb_pending_job_action_plan_t *plan);
int deneb_pending_job_file_finish_action(
    const char *path,
    const deneb_pending_job_action_plan_t *plan);
int deneb_pending_job_file_check_upload(const deneb_pending_job_file_t *job,
                                        const char *candidate_path,
                                        const char *fallback_name,
                                        deneb_pending_job_upload_check_t *check);
int deneb_pending_job_file_check_upload_default(
    const char *candidate_path,
    const char *fallback_name,
    deneb_pending_job_upload_check_t *check);
int deneb_pending_job_file_display_name(const deneb_pending_job_file_t *job,
                                        char *out, size_t out_sz);
int deneb_pending_job_file_display_value(const char *value,
                                         char *out, size_t out_sz);
int deneb_pending_job_file_default_display_name(char *out, size_t out_sz);
int deneb_pending_job_file_is_pending(const deneb_pending_job_file_t *job);
int deneb_pending_job_file_has_path(const deneb_pending_job_file_t *job);
int deneb_pending_job_file_has_conflict(const deneb_pending_job_file_t *job);
int deneb_pending_job_file_same_path(const char *pending_path,
                                     const char *candidate_path);
int deneb_pending_job_file_clear(const char *path);
int deneb_pending_job_file_clear_default(void);
int deneb_pending_job_file_mark_handled(const char *path);
int deneb_pending_job_file_read_raw_array(const char *path,
                                          char *out,
                                          size_t out_sz,
                                          size_t *out_len);

#endif
