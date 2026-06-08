/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_SYSTEM_LANGUAGE_H
#define DENEB_COMMON_SYSTEM_LANGUAGE_H

#include <stddef.h>

#define DENEB_SYSTEM_LANGUAGE_DEFAULT "en"
#define DENEB_SYSTEM_LANGUAGE_READ_COMMAND \
    "uci -q get deneb.system.language 2>/dev/null"

typedef struct {
    const char *code;
    const char *label_key;
} deneb_system_language_choice_t;

size_t deneb_system_language_choice_count(void);
const deneb_system_language_choice_t *
deneb_system_language_choice(size_t index);
int deneb_system_language_code_is_valid(const char *code);
void deneb_system_language_copy_or_default(const char *value,
                                           char *out,
                                           size_t out_sz);
void deneb_system_language_read(char *out, size_t out_sz);
int deneb_system_language_format_save_command(const char *code,
                                              char *out,
                                              size_t out_sz);

#endif
