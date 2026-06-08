/* SPDX-License-Identifier: MPL-2.0 */
#include "print_backend_route.h"

#include <stdio.h>

deneb_print_backend_route_t deneb_print_backend_route(deneb_print_backend_t backend)
{
    deneb_print_backend_route_t route;

    (void)backend;
    route.backend = DENEB_PRINT_BACKEND_NATIVE;
    route.status_url = DENEB_PRINTSVC_STATUS_URL;
    route.command_url = DENEB_PRINTSVC_COMMAND_URL;

    return route;
}

deneb_print_backend_route_t deneb_print_backend_route_detect(void)
{
    return deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
}

const char *deneb_print_backend_name(deneb_print_backend_t backend)
{
    (void)backend;
    return "native";
}

int deneb_print_backend_is_native(deneb_print_backend_t backend)
{
    (void)backend;
    return 1;
}

int deneb_print_backend_route_json_fields(const deneb_print_backend_route_t *route,
                                          char *out, size_t out_sz)
{
    deneb_print_backend_route_t fallback;

    if (!out || out_sz == 0)
        return -1;

    if (!route) {
        fallback = deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
        route = &fallback;
    }

    return snprintf(out, out_sz,
                    "\"print_backend\":\"%s\","
                    "\"print_backend_status_url\":\"%s\","
                    "\"print_backend_command_url\":\"%s\"",
                    deneb_print_backend_name(route->backend),
                    route->status_url ? route->status_url : "",
                    route->command_url ? route->command_url : "");
}
