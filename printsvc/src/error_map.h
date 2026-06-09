/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_ERROR_MAP_H
#define DENEB_PRINTSVC_ERROR_MAP_H

#include <stddef.h>

typedef enum {
    DENEB_ERROR_NONE = 0,
    DENEB_ERROR_MARLIN_FAULT,
    DENEB_ERROR_THERMAL,
    DENEB_ERROR_ENDSTOP,
    DENEB_ERROR_SERIAL,
    DENEB_ERROR_STORAGE,
    DENEB_ERROR_COMMAND,
    DENEB_ERROR_UNKNOWN
} deneb_error_code_t;

typedef struct {
    deneb_error_code_t code;
    const char *key;
    const char *category;
    char detail[128];
} deneb_error_t;

void deneb_error_clear(deneb_error_t *error);
deneb_error_t deneb_error_from_marlin_line(const char *line);
int deneb_error_line_is_recoverable_serial(const char *line);
deneb_error_t deneb_error_make(deneb_error_code_t code, const char *detail);
const char *deneb_error_code_key(deneb_error_code_t code);
const char *deneb_error_code_category(deneb_error_code_t code);

#endif
