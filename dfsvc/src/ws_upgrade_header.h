/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_WS_UPGRADE_HEADER_H
#define DENEB_WS_UPGRADE_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool ws_upgrade_header_is_text(const char *header, size_t length)
{
    return memchr(header, '\0', length) == NULL;
}

#endif
