/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "macro_registry.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int deneb_macro_name_is_safe(const char *name)
{
    size_t len;

    if (!name || !*name)
        return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return 0;

    len = strlen(name);
    return len > 6 && strcmp(name + len - 6, ".gcode") == 0;
}

static int macro_join_path(const char *dir,
                           const char *macro_name,
                           char *path,
                           size_t path_sz)
{
    int n;

    if (!dir || !*dir || !macro_name || !path || path_sz == 0)
        return -1;

    n = snprintf(path, path_sz, "%s/%s", dir, macro_name);
    if (n < 0 || (size_t)n >= path_sz)
        return -1;

    return 0;
}

int deneb_macro_resolve_from_dirs(const char *macro_name,
                                  const char *override_dir,
                                  const char *stock_dir,
                                  char *path,
                                  size_t path_sz)
{
    char candidate[512];

    if (!deneb_macro_name_is_safe(macro_name) || !path || path_sz == 0)
        return -1;

    if (override_dir && override_dir[0] &&
        macro_join_path(override_dir, macro_name, candidate,
                        sizeof(candidate)) == 0 &&
        access(candidate, R_OK) == 0) {
        return macro_join_path(override_dir, macro_name, path, path_sz);
    }

    if (stock_dir && stock_dir[0])
        return macro_join_path(stock_dir, macro_name, path, path_sz);

    return -1;
}

int deneb_macro_resolve(const char *macro_name, char *path, size_t path_sz)
{
    return deneb_macro_resolve_from_dirs(macro_name,
                                         DENEB_PRINTSVC_MACRO_DIR,
                                         DENEB_PRINTSVC_STOCK_MACRO_RECOVERY_DIR,
                                         path,
                                         path_sz);
}
