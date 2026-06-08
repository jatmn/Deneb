/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MACRO_REGISTRY_H
#define DENEB_PRINTSVC_MACRO_REGISTRY_H

#include <stddef.h>

int deneb_macro_name_is_safe(const char *name);
int deneb_macro_resolve_from_dir(const char *macro_name,
                                 const char *macro_dir,
                                 char *path,
                                 size_t path_sz);
int deneb_macro_resolve(const char *macro_name, char *path, size_t path_sz);

#endif
