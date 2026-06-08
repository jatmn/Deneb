/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_DIAGNOSTICS_EXPORT_H
#define DENEB_DIAGNOSTICS_EXPORT_H

#include <stddef.h>

#define DENEB_DIAGNOSTICS_EXPORT_TMP_DIR "/tmp/deneb-log-export"
#define DENEB_DIAGNOSTICS_EXPORT_LOG_PATH "/tmp/deneb-log-export.log"

const char *deneb_diagnostics_export_usb_available_command(void);
int deneb_diagnostics_export_format_command(char *out, size_t out_sz);

#endif
