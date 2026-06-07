/* SPDX-License-Identifier: MPL-2.0 */
#include "command_reply.h"
#include "json_string.h"

#include <stdio.h>

int deneb_command_reply_json(char *reply, size_t reply_sz,
                             const char *status, const char *message)
{
    char escaped_status[32];
    char escaped_message[192];
    int n;

    if (!reply || reply_sz == 0)
        return -1;

    deneb_json_escape_string(status ? status : "", escaped_status,
                             sizeof(escaped_status));
    deneb_json_escape_string(message ? message : "", escaped_message,
                             sizeof(escaped_message));
    n = snprintf(reply, reply_sz, "{\"status\":\"%s\",\"message\":\"%s\"}",
                 escaped_status, escaped_message);
    if (n < 0 || (size_t)n >= reply_sz)
        return -1;
    return 0;
}

int deneb_command_reply_ok(char *reply, size_t reply_sz, const char *message)
{
    return deneb_command_reply_json(reply, reply_sz, "ok", message);
}

int deneb_command_reply_error(char *reply, size_t reply_sz, const char *message)
{
    return deneb_command_reply_json(reply, reply_sz, "error", message);
}
