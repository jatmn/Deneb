/* SPDX-License-Identifier: MPL-2.0 */
#include "print_history.h"

#include "json_file.h"
#include "json_string.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void deneb_print_history_read_default_array_or_empty(char *out, size_t out_sz)
{
    deneb_json_file_read_array_or_empty(DENEB_PRINT_HISTORY_PATH, out, out_sz);
}

static void format_utc_time(long long unix_time, char *out, size_t out_sz)
{
    time_t t = (time_t)unix_time;
    struct tm *tm;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';
    if (unix_time <= 0)
        return;

    tm = gmtime(&t);
    if (!tm)
        return;
    snprintf(out, out_sz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static int build_entry_json(const deneb_print_history_entry_t *entry,
                            char *out, size_t out_sz)
{
    char name[256];
    char uuid[128];
    char source[128];
    char state[64];
    char started[32];
    char finished[32];
    int n;

    if (!entry || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(entry->name ? entry->name : "", name,
                             sizeof(name));
    deneb_json_escape_string(entry->uuid ? entry->uuid : "", uuid,
                             sizeof(uuid));
    deneb_json_escape_string(entry->source ? entry->source : "", source,
                             sizeof(source));
    deneb_json_escape_string(entry->state ? entry->state : "", state,
                             sizeof(state));
    format_utc_time(entry->started_at, started, sizeof(started));
    format_utc_time(entry->finished_at, finished, sizeof(finished));

    n = snprintf(out, out_sz,
                 "{\"name\":\"%s\",\"uuid\":\"%s\",\"source\":\"%s\","
                 "\"state\":\"%s\",\"time_total\":%d,"
                 "\"time_elapsed\":%d,\"progress\":%.1f,"
                 "\"started_at\":\"%s\",\"finished_at\":\"%s\"}",
                 name, uuid, source, state,
                 entry->time_total, entry->time_elapsed, entry->progress,
                 started, finished);
    return n >= 0 && (size_t)n < out_sz ? 0 : -1;
}

static int read_history_file(const char *path, char *buf, size_t buf_sz)
{
    FILE *f;
    size_t n;

    if (!path || !buf || buf_sz == 0)
        return -1;
    buf[0] = '\0';

    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(buf, 1, buf_sz - 1, f);
    if (ferror(f)) {
        fclose(f);
        buf[0] = '\0';
        return -1;
    }
    fclose(f);
    buf[n] = '\0';
    return 0;
}

int deneb_print_history_append_entry(const char *path,
                                     const deneb_print_history_entry_t *entry)
{
    char entry_json[1024];
    char history[65536];
    char tmp_path[320];
    FILE *out;

    if (!path || !*path || !entry)
        return -1;
    if (build_entry_json(entry, entry_json, sizeof(entry_json)) != 0)
        return -1;
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) < 0 ||
        strlen(path) + 4 >= sizeof(tmp_path))
        return -1;

    if (read_history_file(path, history, sizeof(history)) != 0)
        history[0] = '\0';

    out = fopen(tmp_path, "wb");
    if (!out)
        return -1;

    if (history[0] != '[') {
        fprintf(out, "[\n%s\n]\n", entry_json);
    } else {
        char *p = strrchr(history, ']');
        if (p && p > history) {
            char *end;
            *p = '\0';
            end = p - 1;
            while (end > history &&
                   (*end == '\n' || *end == '\r' ||
                    *end == ' ' || *end == '\t')) {
                end--;
            }
            *(end + 1) = '\0';
            fprintf(out, "%s,\n%s\n]\n", history, entry_json);
        } else {
            fprintf(out, "[\n%s\n]\n", entry_json);
        }
    }

    if (fclose(out) != 0)
        return -1;
    if (rename(tmp_path, path) != 0)
        return -1;
    return 0;
}
