/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINTER_IDENTITY_H
#define DENEB_COMMON_PRINTER_IDENTITY_H

#include <stddef.h>

#define DENEB_PRINTER_DEFAULT_HOSTNAME "deneb"
#define DENEB_PRINTER_DEFAULT_GUID "00000000-0000-0000-0000-000000000000"
#define DENEB_PRINTER_UNAVAILABLE_ID "Unavailable"

void deneb_printer_identity_copy_line_or_default(const char *value,
                                                 const char *fallback,
                                                 char *out,
                                                 size_t out_sz);
void deneb_printer_identity_hostname(char *out, size_t out_sz);
void deneb_printer_identity_friendly_name(char *out, size_t out_sz);
void deneb_printer_identity_guid(char *out, size_t out_sz);
void deneb_printer_identity_display_id(char *out, size_t out_sz);

#endif
