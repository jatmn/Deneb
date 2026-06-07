/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_STATUS_PARSER_H
#define DENEB_PRINTSVC_STATUS_PARSER_H

#include "status.h"

typedef enum {
    DENEB_PARSE_NO_MATCH = 0,
    DENEB_PARSE_TEMPERATURE,
    DENEB_PARSE_POSITION,
    DENEB_PARSE_VERSION,
    DENEB_PARSE_FAULT
} deneb_status_parse_result_t;

deneb_status_parse_result_t deneb_status_parse_marlin_line(deneb_status_t *status,
                                                           const char *line);

#endif
