/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_JSON_FILE_H
#define DENEB_COMMON_JSON_FILE_H

#include <stddef.h>

void deneb_json_file_read_array_or_empty(const char *path,
                                         char *out,
                                         size_t out_sz);

#endif
