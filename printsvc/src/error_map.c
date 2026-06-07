/* SPDX-License-Identifier: MPL-2.0 */
#include "error_map.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int contains_ci(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return 0;

    needle_len = strlen(needle);
    if (needle_len == 0)
        return 1;

    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (haystack[i + j] && j < needle_len &&
               tolower((unsigned char)haystack[i + j]) ==
               tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == needle_len)
            return 1;
    }

    return 0;
}

const char *deneb_error_code_key(deneb_error_code_t code)
{
    switch (code) {
        case DENEB_ERROR_MARLIN_FAULT:
            return "marlin_fault";
        case DENEB_ERROR_THERMAL:
            return "thermal_fault";
        case DENEB_ERROR_ENDSTOP:
            return "endstop_fault";
        case DENEB_ERROR_SERIAL:
            return "serial_fault";
        case DENEB_ERROR_STORAGE:
            return "storage_fault";
        case DENEB_ERROR_COMMAND:
            return "command_fault";
        case DENEB_ERROR_UNKNOWN:
            return "unknown_fault";
        case DENEB_ERROR_NONE:
        default:
            return "none";
    }
}

const char *deneb_error_code_category(deneb_error_code_t code)
{
    switch (code) {
        case DENEB_ERROR_THERMAL:
            return "thermal";
        case DENEB_ERROR_ENDSTOP:
            return "motion";
        case DENEB_ERROR_SERIAL:
            return "serial";
        case DENEB_ERROR_STORAGE:
            return "storage";
        case DENEB_ERROR_COMMAND:
            return "command";
        case DENEB_ERROR_MARLIN_FAULT:
            return "firmware";
        case DENEB_ERROR_UNKNOWN:
            return "unknown";
        case DENEB_ERROR_NONE:
        default:
            return "none";
    }
}

void deneb_error_clear(deneb_error_t *error)
{
    if (!error)
        return;

    error->code = DENEB_ERROR_NONE;
    error->key = deneb_error_code_key(error->code);
    error->category = deneb_error_code_category(error->code);
    error->detail[0] = '\0';
}

deneb_error_t deneb_error_make(deneb_error_code_t code, const char *detail)
{
    deneb_error_t error;

    error.code = code;
    error.key = deneb_error_code_key(code);
    error.category = deneb_error_code_category(code);
    snprintf(error.detail, sizeof(error.detail), "%s", detail ? detail : "");
    return error;
}

deneb_error_t deneb_error_from_marlin_line(const char *line)
{
    deneb_error_code_t code = DENEB_ERROR_UNKNOWN;

    if (!line || !*line)
        return deneb_error_make(DENEB_ERROR_NONE, "");

    if (contains_ci(line, "thermal") ||
        contains_ci(line, "temp") ||
        contains_ci(line, "heater") ||
        contains_ci(line, "mintemp") ||
        contains_ci(line, "maxtemp")) {
        code = DENEB_ERROR_THERMAL;
    } else if (contains_ci(line, "endstop") ||
               contains_ci(line, "homing failed") ||
               contains_ci(line, "home failed")) {
        code = DENEB_ERROR_ENDSTOP;
    } else if (contains_ci(line, "serial") ||
               contains_ci(line, "line number") ||
               contains_ci(line, "checksum") ||
               contains_ci(line, "resend")) {
        code = DENEB_ERROR_SERIAL;
    } else if (contains_ci(line, "unknown command") ||
               contains_ci(line, "bad command")) {
        code = DENEB_ERROR_COMMAND;
    } else if (contains_ci(line, "error:") ||
               contains_ci(line, "fault") ||
               contains_ci(line, "halted") ||
               contains_ci(line, "!!")) {
        code = DENEB_ERROR_MARLIN_FAULT;
    }

    return deneb_error_make(code, line);
}
