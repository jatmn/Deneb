/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_JOB_FILE_H
#define DENEB_COMMON_PRINT_JOB_FILE_H

#include <stddef.h>

#define DENEB_PRINT_JOB_USB_SCAN_DIR "/mnt/sda1"
#define DENEB_PRINT_JOB_LOCAL_SCAN_DIR "/home/3D"
#define DENEB_PRINT_JOB_SPOOL_DIR "/home/3D/deneb-uploads"

typedef struct {
    char material_guid[64];
    char nozzle_size[24];
} deneb_print_job_file_metadata_t;

typedef struct {
    const char *path;
    const char *source;
    const char *uuid;
    float bed_target;
    float nozzle_target;
} deneb_print_job_start_plan_t;

typedef struct {
    char filename[128];
    char dest_path[256];
} deneb_print_job_upload_storage_plan_t;

void deneb_print_job_file_metadata_init(deneb_print_job_file_metadata_t *meta);
void deneb_print_job_start_plan_init(deneb_print_job_start_plan_t *plan);
void deneb_print_job_upload_storage_plan_init(
    deneb_print_job_upload_storage_plan_t *plan);
int deneb_print_job_start_plan_prepare(const char *path, const char *source,
                                       const char *uuid, float bed_target,
                                       float nozzle_target,
                                       deneb_print_job_start_plan_t *plan);
int deneb_print_job_start_plan_file(const char *path, const char *source,
                                    deneb_print_job_start_plan_t *plan);
int deneb_print_job_file_metadata_extract_value(const char *buf,
                                                const char *key,
                                                char *out,
                                                size_t out_sz);
int deneb_print_job_file_metadata_load(const char *path,
                                       deneb_print_job_file_metadata_t *meta);
int deneb_print_job_file_sanitize_name(const char *name, char *out,
                                       size_t out_sz);
int deneb_print_job_file_spool_path(const char *name, char *out,
                                    size_t out_sz);
int deneb_print_job_file_upload_storage_plan(
    const char *filename,
    deneb_print_job_upload_storage_plan_t *plan);
int deneb_print_job_file_store_upload(const char *src_path,
                                      const char *dest_path);

#endif
