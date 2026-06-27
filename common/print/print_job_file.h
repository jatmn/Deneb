/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_JOB_FILE_H
#define DENEB_COMMON_PRINT_JOB_FILE_H

#include <stddef.h>

#define DENEB_PRINT_JOB_USB_SCAN_DIR "/mnt/sda1"
#define DENEB_PRINT_JOB_LOCAL_SCAN_DIR "/home/3D"
#define DENEB_PRINT_JOB_SPOOL_DIR "/home/3D/deneb-uploads"
#define DENEB_ACTIVE_THUMB_PATH "/tmp/deneb-active-thumb.rgb565"

#define DENEB_BUILD_VOLUME_X_MIN_MM 0.0f
#define DENEB_BUILD_VOLUME_X_MAX_MM 223.0f
#define DENEB_BUILD_VOLUME_Y_MIN_MM 0.0f
#define DENEB_BUILD_VOLUME_Y_MAX_MM 220.0f
#define DENEB_BUILD_VOLUME_Z_MIN_MM 0.0f
#define DENEB_BUILD_VOLUME_Z_MAX_MM 205.0f

typedef struct {
    char material_guid[64];
    char nozzle_size[24];
    int print_time_seconds;
    int has_bounds;
    int bounds_invalid;
    unsigned bounds_field_mask;
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;
} deneb_print_job_file_metadata_t;

typedef struct {
    const char *path;
    const char *source;
    const char *uuid;
    const char *cloud_job_id;
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
                                       const char *uuid,
                                       const char *cloud_job_id,
                                       float bed_target,
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
int deneb_print_job_file_has_extension(const char *name, const char *extension);
int deneb_print_job_file_replace_extension(const char *name,
                                           const char *extension,
                                           char *out,
                                           size_t out_sz);
int deneb_print_job_file_extract_ufp_model_gcode(const char *ufp_path,
                                                 const char *gcode_path);
int deneb_print_job_file_extract_ufp_thumbnail(const char *ufp_path,
                                               const char *out_path);
void deneb_print_job_file_clear_active_thumbnail(void);
int deneb_print_job_file_sanitize_name(const char *name, char *out,
                                       size_t out_sz);
int deneb_print_job_file_spool_path(const char *name, char *out,
                                    size_t out_sz);
int deneb_print_job_file_upload_storage_plan(
    const char *filename,
    deneb_print_job_upload_storage_plan_t *plan);
int deneb_print_job_file_store_upload(const char *src_path,
                                      const char *dest_path);

int deneb_print_job_file_check_build_volume(
    const deneb_print_job_file_metadata_t *meta,
    char *out_error, size_t out_error_sz);

#endif
