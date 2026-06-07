/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_COMMAND_REPLY_H
#define DENEB_PRINTSVC_COMMAND_REPLY_H

#include <stddef.h>

int deneb_command_reply_json(char *reply, size_t reply_sz,
                             const char *status, const char *message);
int deneb_command_reply_ok(char *reply, size_t reply_sz,
                           const char *message);
int deneb_command_reply_error(char *reply, size_t reply_sz,
                              const char *message);

#endif
