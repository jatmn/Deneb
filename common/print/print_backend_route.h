/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_BACKEND_ROUTE_H
#define DENEB_PRINT_BACKEND_ROUTE_H

#include <stddef.h>

typedef enum {
    DENEB_PRINT_BACKEND_COORDINATOR = 0,
    DENEB_PRINT_BACKEND_NATIVE
} deneb_print_backend_t;

typedef struct {
    deneb_print_backend_t backend;
    const char *status_url;
    const char *command_url;
} deneb_print_backend_route_t;

#define DENEB_COORDINATOR_STATUS_URL "tcp://127.0.0.1:5565"
#define DENEB_COORDINATOR_COMMAND_URL "tcp://127.0.0.1:5566"
#define DENEB_PRINTSVC_STATUS_URL "tcp://127.0.0.1:5555"
#define DENEB_PRINTSVC_COMMAND_URL "tcp://127.0.0.1:5556"

int deneb_print_backend_parse_override(const char *value,
                                       deneb_print_backend_t *backend);
deneb_print_backend_t deneb_print_backend_from_flag_text(const char *value);
deneb_print_backend_route_t deneb_print_backend_route(deneb_print_backend_t backend);
deneb_print_backend_route_t deneb_print_backend_route_detect(void);
const char *deneb_print_backend_name(deneb_print_backend_t backend);
int deneb_print_backend_route_json_fields(const deneb_print_backend_route_t *route,
                                          char *out, size_t out_sz);

#endif
