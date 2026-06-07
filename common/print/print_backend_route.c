/* SPDX-License-Identifier: MPL-2.0 */
#include "print_backend_route.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

int deneb_print_backend_parse_override(const char *value,
                                       deneb_print_backend_t *backend)
{
    if (!value || !*value || !backend)
        return -1;

    if (streq(value, "native") || streq(value, "1")) {
        *backend = DENEB_PRINT_BACKEND_NATIVE;
        return 0;
    }

    if (streq(value, "coordinator") || streq(value, "stock") || streq(value, "0")) {
        *backend = DENEB_PRINT_BACKEND_COORDINATOR;
        return 0;
    }

    return -1;
}

deneb_print_backend_t deneb_print_backend_from_flag_text(const char *value)
{
    return value && value[0] == '1' &&
           (value[1] == '\0' || value[1] == '\n' || value[1] == '\r') ?
        DENEB_PRINT_BACKEND_NATIVE : DENEB_PRINT_BACKEND_COORDINATOR;
}

deneb_print_backend_route_t deneb_print_backend_route(deneb_print_backend_t backend)
{
    deneb_print_backend_route_t route;

    if (backend == DENEB_PRINT_BACKEND_NATIVE) {
        route.backend = DENEB_PRINT_BACKEND_NATIVE;
        route.status_url = DENEB_PRINTSVC_STATUS_URL;
        route.command_url = DENEB_PRINTSVC_COMMAND_URL;
    } else {
        route.backend = DENEB_PRINT_BACKEND_COORDINATOR;
        route.status_url = DENEB_COORDINATOR_STATUS_URL;
        route.command_url = DENEB_COORDINATOR_COMMAND_URL;
    }

    return route;
}

deneb_print_backend_route_t deneb_print_backend_route_detect(void)
{
    deneb_print_backend_t backend;
    const char *env = getenv("DENEB_PRINTSVC_BACKEND");
    FILE *f;
    char buf[32];

    if (deneb_print_backend_parse_override(env, &backend) == 0)
        return deneb_print_backend_route(backend);

    f = popen("uci -q get deneb.printsvc.enabled 2>/dev/null", "r");
    if (f) {
        if (fgets(buf, sizeof(buf), f))
            backend = deneb_print_backend_from_flag_text(buf);
        else
            backend = DENEB_PRINT_BACKEND_COORDINATOR;
        pclose(f);
    } else {
        backend = DENEB_PRINT_BACKEND_COORDINATOR;
    }

    return deneb_print_backend_route(backend);
}

const char *deneb_print_backend_name(deneb_print_backend_t backend)
{
    return backend == DENEB_PRINT_BACKEND_NATIVE ? "native" : "coordinator";
}
