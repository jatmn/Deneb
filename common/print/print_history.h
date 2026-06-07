/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_HISTORY_H
#define DENEB_COMMON_PRINT_HISTORY_H

#include <stddef.h>

#define DENEB_PRINT_HISTORY_PATH "/home/3D/deneb-print-history.json"

void deneb_print_history_read_default_array_or_empty(char *out, size_t out_sz);

#endif
