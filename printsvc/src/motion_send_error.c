/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_send_error.h"

#include "motion_sender.h"

deneb_error_code_t deneb_motion_send_error_code(int rc)
{
    return rc == DENEB_MOTION_SEND_SERIAL ? DENEB_ERROR_SERIAL :
                                            DENEB_ERROR_COMMAND;
}

const char *deneb_motion_send_error_name(int rc)
{
    switch (rc) {
        case DENEB_MOTION_SEND_INVALID:
            return "invalid_packet";
        case DENEB_MOTION_SEND_FLOW_FULL:
            return "flow_full";
        case DENEB_MOTION_SEND_SERIAL:
            return "serial_write";
        case 0:
            return "ok";
        default:
            return "unknown";
    }
}
