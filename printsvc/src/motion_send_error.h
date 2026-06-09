/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_SEND_ERROR_H
#define DENEB_PRINTSVC_MOTION_SEND_ERROR_H

#include "error_map.h"

deneb_error_code_t deneb_motion_send_error_code(int rc);
const char *deneb_motion_send_error_name(int rc);

#endif
