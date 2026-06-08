/* SPDX-License-Identifier: MPL-2.0 */
#include "system_language.h"

#include <stdio.h>
#include <string.h>

static const deneb_system_language_choice_t language_choices[] = {
    {"en", "language.en"},
    {"nl", "language.nl"},
    {"de", "language.de"},
    {"fr", "language.fr"},
    {"zh-Hans", "language.zh_Hans"},
    {"en-pirate", "language.en_pirate"},
    {"en-1337", "language.en_1337"},
};

size_t deneb_system_language_choice_count(void)
{
    return sizeof(language_choices) / sizeof(language_choices[0]);
}

const deneb_system_language_choice_t *
deneb_system_language_choice(size_t index)
{
    if (index >= deneb_system_language_choice_count())
        return NULL;
    return &language_choices[index];
}

int deneb_system_language_code_is_valid(const char *code)
{
    size_t i;

    if (!code || !*code)
        return 0;

    for (i = 0; i < deneb_system_language_choice_count(); i++) {
        if (strcmp(language_choices[i].code, code) == 0)
            return 1;
    }

    return 0;
}

void deneb_system_language_copy_or_default(const char *value,
                                           char *out,
                                           size_t out_sz)
{
    size_t len;

    if (!out || out_sz == 0)
        return;

    snprintf(out, out_sz, "%s",
             value && *value ? value : DENEB_SYSTEM_LANGUAGE_DEFAULT);
    len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                       out[len - 1] == ' ' || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }

    if (!deneb_system_language_code_is_valid(out))
        snprintf(out, out_sz, "%s", DENEB_SYSTEM_LANGUAGE_DEFAULT);
}

void deneb_system_language_read(char *out, size_t out_sz)
{
    FILE *f;
    char line[32] = "";

    if (!out || out_sz == 0)
        return;

    f = popen(DENEB_SYSTEM_LANGUAGE_READ_COMMAND, "r");
    if (f) {
        (void)fgets(line, sizeof(line), f);
        pclose(f);
    }

    deneb_system_language_copy_or_default(line, out, out_sz);
}

int deneb_system_language_format_save_command(const char *code,
                                              char *out,
                                              size_t out_sz)
{
    int n;

    if (!deneb_system_language_code_is_valid(code) || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "uci -q set deneb.system=system; "
                 "uci -q set deneb.system.language='%s'; "
                 "uci -q commit deneb",
                 code);
    if (n < 0 || n >= (int)out_sz)
        return -1;

    return 0;
}
