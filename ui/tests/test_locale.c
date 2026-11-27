/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "locale.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_overlay(void)
{
    char directory[] = "./deneb-locale-XXXXXX";
    int cwd = open(".", O_RDONLY);
    assert(cwd >= 0);
    assert(mkdtemp(directory) != NULL);
    assert(chdir(directory) == 0);

    FILE *file = fopen("fixture.json", "w");
    assert(file != NULL);
    assert(fputs("{\"menu.status\": \"Old value\",\n"
                 "\"menu.status\": \"Home\",\n"
                 "\"menu.settings\": \"Escaped \\\"quote\\\" \\u00c9\",\n"
                 "\"menu.print\": \"Invalid \\u12xz\",\n"
                 "\"extra.title\": \"Extra\\n%s and %s\",\n"
                 "}", file) >= 0);
    assert(fclose(file) == 0);

    assert(locale_init("fixture") == 0);
    assert(strcmp(locale_get("menu.status"), "Home") == 0);
    const char *settings = locale_get("menu.settings");
    char formatted[64];
    assert(locale_format_ss(formatted, sizeof(formatted), "extra.title", "A", "B") == 0);
    assert(strcmp(formatted, "Extra\nA and B") == 0);
    assert(strcmp(settings, "Escaped \"quote\" É") == 0);
    assert(strcmp(locale_get("menu.print"), "Print from USB") == 0);

    assert(locale_set("en") == 0);
    assert(strcmp(locale_get("menu.settings"), "Settings") == 0);
    const char extra[] = "extra.title";
    assert(locale_get(extra) == extra);
    assert(unlink("fixture.json") == 0);
    assert(fchdir(cwd) == 0);
    assert(close(cwd) == 0);
    assert(rmdir(directory) == 0);
}

int main(int argc, char **argv)
{
    static const struct { const char *code; const char *home; } languages[] = {
        {"en", "Home"},
        {"de", "Startseite"},
        {"en-1337", "H0m3"},
        {"en-pirate", "Home Port"},
        {"fr", "Accueil"},
        {"nl", "Startpagina"},
        {"zh-Hans", "主页"},
    };
    const char key[] = "menu.status";
    assert(locale_get(key) == key);
    test_overlay();

    assert(argc == 2);
    assert(chdir(argv[1]) == 0);
    for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]); i++) {
        assert(locale_set(languages[i].code) == 0);
        assert(strcmp(locale_current(), languages[i].code) == 0);
        assert(strcmp(locale_get(key), languages[i].home) == 0);
        assert(strcmp(locale_get("print_conflict.message_fmt"),
                      "This job was sliced for %s, but %s is loaded.") == 0);
    }

    char formatted[128];
    assert(locale_format_s(formatted, sizeof(formatted), "print.selected_fmt", "A.gcode") == 0);
    assert(strcmp(formatted, "已选择\nA.gcode\n\n开始前请检查材料/喷嘴。") == 0);
    assert(locale_set("missing") == 0);
    assert(strcmp(locale_get(key), "Home") == 0);
    locale_deinit();
    assert(locale_get(key) == key);
    return 0;
}
