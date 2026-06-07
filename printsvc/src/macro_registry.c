/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "macro_registry.h"

#include <stdio.h>
#include <string.h>

static int macro_name_safe(const char *name)
{
    if (!name || !*name)
        return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return 0;
    return strstr(name, ".gcode") != NULL;
}

int deneb_macro_resolve(const char *macro_name, char *path, size_t path_sz)
{
    int n;

    if (!macro_name_safe(macro_name) || !path || path_sz == 0)
        return -1;

    n = snprintf(path, path_sz, "%s/%s", DENEB_PRINTSVC_MACRO_DIR, macro_name);
    if (n < 0 || (size_t)n >= path_sz)
        return -1;

    return 0;
}
