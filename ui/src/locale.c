/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Locale/i18n implementation.
 * Loads JSON locale files from /etc/deneb/locales/<lang>.json
 * Uses a simple key-value store (no full JSON parser dependency).
 * Falls back to embedded English defaults for missing keys.
 *
 * Design: Minimal memory. One locale loaded at a time.
 * JSON parsing uses a lightweight state machine, not a library.
 */

#include "locale.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOCALE_DIR      "/etc/deneb/locales"
#define MAX_ENTRIES     64
#define MAX_KEY_LEN     48
#define MAX_VAL_LEN     128
#define MAX_LANG_LEN    8

typedef struct {
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
} locale_entry_t;

static locale_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;
static char current_lang[MAX_LANG_LEN] = "en";

/* Embedded English defaults - matches en.json */
static const struct { const char *key; const char *val; } en_defaults[] = {
    {"menu.status", "Status"},
    {"menu.print", "Print from USB"},
    {"menu.material", "Material"},
    {"menu.settings", "Settings"},
    {"status.idle", "Idle"},
    {"status.printing", "Printing"},
    {"status.paused", "Paused"},
    {"status.heating", "Heating"},
    {"status.cooling", "Cooling"},
    {"status.error", "Error"},
    {"status.complete", "Complete"},
    {"status.preparing", "Preparing"},
    {"status.nozzle", "Nozzle"},
    {"status.bed", "Bed"},
    {"status.no_file", "No file loaded"},
    {"print.no_files", "No printable files found"},
    {"print.start", "Start Print"},
    {"print.pause", "Pause"},
    {"print.resume", "Resume"},
    {"print.cancel", "Cancel"},
    {"material.load", "Load Material"},
    {"material.unload", "Unload Material"},
    {"material.insert_material", "Insert material and select an option below."},
    {"settings.language", "Language"},
    {"settings.about", "About Deneb"},
    {"settings.network", "Network"},
    {"settings.maintenance", "Maintenance"},
    {"about.description", "Deneb is a community firmware mod for the UltiMaker 2+ Connect."},
    {"about.license", "Licensed under MPL-2.0"},
    {"error.title", "Error"},
    {"error.er_code", "ER code"},
    {"error.ok", "OK"},
    {"confirm.title", "Confirm"},
    {"confirm.yes", "Yes"},
    {"confirm.no", "No"},
    {NULL, NULL}
};

/**
 * Simple JSON string value parser.
 * Handles: "key": "value" pairs, with escaped quotes.
 * Does NOT handle nested objects, arrays, or non-string values.
 * This is intentional: locale files are flat key-value maps.
 */
static int parse_locale_json(const char *json, locale_entry_t *out, int max_entries)
{
    int count = 0;
    const char *p = json;

    while (*p && count < max_entries) {
        /* Find next key */
        p = strchr(p, '"');
        if (!p) break;
        p++; /* skip opening quote */

        /* Read key */
        const char *key_start = p;
        while (*p && *p != '"') p++;
        if (!*p) break;
        size_t key_len = p - key_start;
        p++; /* skip closing quote */

        /* Find colon */
        while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
        if (*p != '"') continue; /* expect string value */
        p++; /* skip opening quote */

        /* Read value */
        const char *val_start = p;
        while (*p) {
            if (*p == '\\') {
                p += 2; /* skip escaped char */
                continue;
            }
            if (*p == '"') break;
            p++;
        }
        if (!*p) break;
        size_t val_len = p - val_start;
        p++; /* skip closing quote */

        /* Store entry */
        if (key_len < MAX_KEY_LEN && val_len < MAX_VAL_LEN) {
            memcpy(out[count].key, key_start, key_len);
            out[count].key[key_len] = '\0';
            memcpy(out[count].val, val_start, val_len);
            out[count].val[val_len] = '\0';
            count++;
        }
    }

    return count;
}

int locale_init(const char *lang)
{
    entry_count = 0;
    memset(entries, 0, sizeof(entries));

    /* Load embedded English defaults first */
    for (int i = 0; en_defaults[i].key; i++) {
        strncpy(entries[entry_count].key, en_defaults[i].key, MAX_KEY_LEN - 1);
        strncpy(entries[entry_count].val, en_defaults[i].val, MAX_VAL_LEN - 1);
        entry_count++;
        if (entry_count >= MAX_ENTRIES) break;
    }

    /* If not English, try to load from disk */
    if (lang && strcmp(lang, "en") != 0) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s.json", LOCALE_DIR, lang);

        FILE *f = fopen(path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (size > 0 && size < 8192) {
                char *buf = malloc(size + 1);
                if (buf) {
                    fread(buf, 1, size, f);
                    buf[size] = '\0';

                    /* Parse and overlay on defaults */
                    locale_entry_t loaded[MAX_ENTRIES];
                    int loaded_count = parse_locale_json(buf, loaded, MAX_ENTRIES);

                    for (int i = 0; i < loaded_count; i++) {
                        /* Find existing entry and update, or add new */
                        int found = 0;
                        for (int j = 0; j < entry_count; j++) {
                            if (strcmp(entries[j].key, loaded[i].key) == 0) {
                                strncpy(entries[j].val, loaded[i].val, MAX_VAL_LEN - 1);
                                found = 1;
                                break;
                            }
                        }
                        if (!found && entry_count < MAX_ENTRIES) {
                            entries[entry_count] = loaded[i];
                            entry_count++;
                        }
                    }

                    free(buf);
                }
            }
            fclose(f);
        } else {
            fprintf(stderr, "locale: %s not found, using English defaults\n", path);
        }
    }

    strncpy(current_lang, lang ? lang : "en", MAX_LANG_LEN - 1);
    current_lang[MAX_LANG_LEN - 1] = '\0';

    fprintf(stderr, "locale: loaded %d entries for '%s'\n", entry_count, current_lang);
    return 0;
}

const char *locale_get(const char *key)
{
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key) == 0)
            return entries[i].val;
    }
    /* Key not found - return key itself as last resort */
    return key;
}

int locale_set(const char *lang)
{
    return locale_init(lang);
}

const char *locale_current(void)
{
    return current_lang;
}

void locale_deinit(void)
{
    entry_count = 0;
    memset(entries, 0, sizeof(entries));
}
