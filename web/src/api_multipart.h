/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_API_MULTIPART_H
#define DENEB_API_MULTIPART_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef DENEB_UPLOAD_TEMP_DIR
#define DENEB_UPLOAD_TEMP_DIR "/tmp"
#endif

typedef struct {
    off_t offset;
    off_t length;
} multipart_range_t;

typedef struct {
    const char *name;
    char *value;
    size_t capacity;
    bool found;
} multipart_field_t;

/* The caller opens the upload for reading at offset zero. */
int parse_multipart_upload(int fd, const char *boundary,
                           multipart_range_t *file, char *filename, size_t filename_capacity,
                           multipart_field_t *fields, size_t field_count,
                           const atomic_bool *cancelled);
int copy_multipart_range(int input, int output, const multipart_range_t *range,
                         const atomic_bool *cancelled);
int extract_multipart_file(const char *boundary, const char *upload_path,
                           char *out_path, int out_sz,
                           char *filename, int fn_sz);

#endif
