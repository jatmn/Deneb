/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_FIRMWARE_H
#define DENEB_PRINTSVC_MOTION_FIRMWARE_H

#include <stddef.h>

typedef enum {
    DENEB_MOTION_FW_ERROR = -1,
    DENEB_MOTION_FW_CACHE_MATCH = 0,
    DENEB_MOTION_FW_PROGRAM_REQUIRED = 1,
    DENEB_MOTION_FW_PROGRAMMED = 2
} deneb_motion_fw_result_t;

int deneb_motion_firmware_hash_file(const char *path, char out_hex[65]);
deneb_motion_fw_result_t deneb_motion_firmware_check_cache(const char *hex_path,
                                                           const char *cache_path,
                                                           char out_hex[65]);
deneb_motion_fw_result_t deneb_motion_firmware_ensure(const char *hex_path,
                                                      const char *cache_path,
                                                      const char *programmer_path,
                                                      int allow_programming);

#endif
