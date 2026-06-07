/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_HISTORY_H
#define DENEB_COMMON_PRINT_HISTORY_H

#include <stddef.h>

#define DENEB_PRINT_HISTORY_PATH "/home/3D/deneb-print-history.json"

typedef struct {
    const char *name;
    const char *uuid;
    const char *source;
    const char *state;
    int time_total;
    int time_elapsed;
    float progress;
    long long started_at;
    long long finished_at;
} deneb_print_history_entry_t;

void deneb_print_history_read_default_array_or_empty(char *out, size_t out_sz);
int deneb_print_history_append_entry(const char *path,
                                     const deneb_print_history_entry_t *entry);

#endif
