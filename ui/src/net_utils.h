/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Shared network utility functions for wifi_setup and eth_setup.
 * Keep this file minimal — only functions used by both modules.
 */

#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NET_USB_CONFIG_FOUND = 0,
    NET_USB_CONFIG_NO_USB,
    NET_USB_CONFIG_NO_FILE,
} net_usb_config_result_t;

/**
 * Remove trailing CR/LF characters in-place.
 */
void net_chomp(char *s);

/**
 * Trim leading and trailing whitespace in-place.
 */
void net_trim(char *s);

/**
 * Escape a string for safe use in a UCI set command via system().
 * Wraps in single quotes, escaping embedded single quotes.
 * dst must be at least (strlen(src) * 4 + 3) bytes.
 */
void net_uci_escape(char *dst, size_t dst_size, const char *src);

/**
 * Lowercase a string in-place.
 */
void net_str_lower(char *s);

/**
 * Parse a key=value config line in-place.
 * Returns false for blank/comment/malformed lines. The key is trimmed and
 * lowercased; the value is trimmed but otherwise preserved.
 */
bool net_parse_key_value_line(char *line, char **key_out, char **val_out);

/**
 * Run a command and capture stdout into dst with trailing CR/LF removed.
 */
void net_read_command(char *dst, size_t dst_size, const char *cmd);

/**
 * USB mount points to check, NULL-terminated.
 */
extern const char *net_usb_mount_points[];

/**
 * Scan USB mount points for a config file.
 */
net_usb_config_result_t net_find_usb_config(const char *filename,
                                            char *path_out,
                                            int path_size);

#endif /* NET_UTILS_H */
