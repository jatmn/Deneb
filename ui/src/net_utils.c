/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Shared network utility functions.
 */

#include "net_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void net_chomp(char *s)
{
    if (!s)
        return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

void net_trim(char *s)
{
    if (!s || !*s)
        return;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

void net_uci_escape(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    if (!dst || dst_size == 0)
        return;
    dst[0] = '\0';
    if (!src || dst_size < 3)
        return;
    dst[out++] = '\'';
    for (const char *p = src; *p && out + 5 < dst_size; p++) {
        if (*p == '\'') {
            if (out + 4 >= dst_size)
                break;
            dst[out++] = '\'';
            dst[out++] = '\\';
            dst[out++] = '\'';
            dst[out++] = '\'';
        } else {
            dst[out++] = *p;
        }
    }
    if (out + 1 < dst_size)
        dst[out++] = '\'';
    dst[out] = '\0';
}

void net_str_lower(char *s)
{
    for (; *s; s++)
        *s = tolower((unsigned char)*s);
}

bool net_parse_key_value_line(char *line, char **key_out, char **val_out)
{
    if (!line || !key_out || !val_out)
        return false;

    net_chomp(line);

    char *p = line;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (!*p || *p == '#' || *p == ';')
        return false;

    char *eq = strchr(p, '=');
    if (!eq)
        return false;

    *eq = '\0';
    char *key = p;
    char *val = eq + 1;

    net_trim(key);
    net_trim(val);
    net_str_lower(key);

    *key_out = key;
    *val_out = val;
    return true;
}

void net_read_command(char *dst, size_t dst_size, const char *cmd)
{
    if (!dst || dst_size == 0)
        return;

    dst[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return;

    size_t used = 0;
    while (used + 1 < dst_size && fgets(dst + used, dst_size - used, fp))
        used = strlen(dst);
    pclose(fp);
    net_chomp(dst);
}

const char *net_usb_mount_points[] = {
    "/mnt/sda1",
    "/mnt/usb",
    "/media/usb",
    NULL
};

net_usb_config_result_t net_find_usb_config(const char *filename,
                                            char *path_out,
                                            int path_size)
{
    struct stat st;
    int found_usb = 0;

    if (!filename || !path_out || path_size <= 0)
        return NET_USB_CONFIG_NO_FILE;

    path_out[0] = '\0';
    for (int i = 0; net_usb_mount_points[i]; i++) {
        if (stat(net_usb_mount_points[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            found_usb = 1;
            snprintf(path_out, path_size, "%s/%s",
                     net_usb_mount_points[i], filename);
            if (stat(path_out, &st) == 0 && S_ISREG(st.st_mode))
                return NET_USB_CONFIG_FOUND;
        }
    }

    return found_usb ? NET_USB_CONFIG_NO_FILE : NET_USB_CONFIG_NO_USB;
}
