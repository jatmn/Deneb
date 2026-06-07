/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_JOB_FILE_H
#define DENEB_COMMON_PRINT_JOB_FILE_H

#include <stddef.h>

#define DENEB_PRINT_JOB_SPOOL_DIR "/home/3D/deneb-uploads"

typedef struct {
    char material_guid[64];
    char nozzle_size[24];
} deneb_print_job_file_metadata_t;

void deneb_print_job_file_metadata_init(deneb_print_job_file_metadata_t *meta);
int deneb_print_job_file_metadata_extract_value(const char *buf,
                                                const char *key,
                                                char *out,
                                                size_t out_sz);
int deneb_print_job_file_metadata_load(const char *path,
                                       deneb_print_job_file_metadata_t *meta);

#endif
