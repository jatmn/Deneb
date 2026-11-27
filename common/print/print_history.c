/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "print_history.h"

#include "json_string.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int next_history_byte(FILE *in, off_t *position)
{
    int c = getc_unlocked(in);
    if (c != EOF)
        (*position)++;
    return c;
}

static int next_history_token(FILE *in, off_t *position)
{
    int c;
    do {
        c = next_history_byte(in, position);
    } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    return c;
}

/* Find the end of an object without treating punctuation in strings as structure. */
static int skip_history_record(FILE *in, off_t *position)
{
    size_t depth = 1;
    int in_string = 0;
    int c;
    while ((c = next_history_byte(in, position)) != EOF) {
        if (in_string) {
            if (c == '\\')
                c = next_history_byte(in, position);
            else if (c == '"')
                in_string = 0;
            if (c < ' ')
                return -1;
        } else if (c == '"') {
            in_string = 1;
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            if (--depth == 0)
                return c == '}' ? 0 : -1;
        }
    }
    return -1;
}

/* Offsets exclude the outer brackets and leading/trailing array whitespace. */
static int scan_history(FILE *in, off_t cutoff, off_t *start, off_t *end)
{
    off_t position = 0;
    *start = -1;
    *end = 0;
    if (fseeko(in, 0, SEEK_SET) != 0 || next_history_token(in, &position) != '[')
        return -1;

    int c = next_history_token(in, &position);
    if (c != ']') {
        for (;;) {
            if (c != '{')
                return -1;
            if (*start < 0 && position - 1 >= cutoff)
                *start = position - 1;
            if (skip_history_record(in, &position) != 0)
                return -1;
            *end = position;
            c = next_history_token(in, &position);
            if (c == ']')
                break;
            if (c != ',')
                return -1;
            c = next_history_token(in, &position);
        }
    }
    return next_history_token(in, &position) == EOF && !ferror(in) ? 0 : -1;
}

static int select_history(FILE *in, size_t budget, size_t *length)
{
    off_t start, end;
    *length = 0;
    if (scan_history(in, 0, &start, &end) != 0)
        return -1;
    if (start < 0)
        return 0;
    if (end - start > (off_t)budget &&
        scan_history(in, end - (off_t)budget, &start, &end) != 0)
        return -1;
    if (start < 0)
        return 0;

    *length = (size_t)(end - start);
    return fseeko(in, start, SEEK_SET);
}

void deneb_print_history_read_default_array_or_empty(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    out[0] = '\0';
    if (out_sz < sizeof("[]"))
        return;

    size_t capacity = out_sz - 1;
    if (capacity > DENEB_PRINT_HISTORY_MAX_BYTES)
        capacity = DENEB_PRINT_HISTORY_MAX_BYTES;
    size_t length = 0;
    FILE *in = fopen(DENEB_PRINT_HISTORY_PATH, "rb");
    if (in) {
        if (select_history(in, capacity - 2, &length) != 0 ||
            fread(out + 1, 1, length, in) != length)
            length = 0;
        fclose(in);
    }
    out[0] = '[';
    out[length + 1] = ']';
    out[length + 2] = '\0';
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

static int copy_history(FILE *in, FILE *out, size_t length)
{
    char buf[1024];
    while (length > 0) {
        size_t count = length < sizeof(buf) ? length : sizeof(buf);
        if (fread(buf, 1, count, in) != count ||
            fwrite(buf, 1, count, out) != count)
            return -1;
        length -= count;
    }
    return 0;
}

int deneb_print_history_append_entry(const char *path,
                                     const deneb_print_history_entry_t *entry)
{
    char entry_json[1024];
    char tmp_path[320];

    if (!path || !*path || !entry)
        return -1;
    if (build_entry_json(entry, entry_json, sizeof(entry_json)) != 0)
        return -1;
    int name_len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (name_len < 0 || (size_t)name_len >= sizeof(tmp_path))
        return -1;

    FILE *in = fopen(path, "rb");
    if (!in && errno != ENOENT)
        return -1;

    int result = -1;
    size_t old_length = 0;
    struct stat previous;
    /* Reserve the new record, outer brackets, whitespace and the comma. */
    size_t budget = DENEB_PRINT_HISTORY_MAX_BYTES - strlen(entry_json) -
                    (sizeof("[\n,\n\n]\n") - 1);
    if (in && (fstat(fileno(in), &previous) != 0 ||
               select_history(in, budget, &old_length) != 0))
        goto done;

    /* The API owner is the sole writer. Clear its unpublished file on retry. */
    if (unlink(tmp_path) != 0 && errno != ENOENT)
        goto done;
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0)
        goto done;
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(tmp_path);
        goto done;
    }

    int written = (!in || fchmod(fd, previous.st_mode & 0777) == 0) &&
                  fputs("[\n", out) != EOF &&
                  (!old_length ||
                   (copy_history(in, out, old_length) == 0 && fputs(",\n", out) != EOF)) &&
                  fputs(entry_json, out) != EOF && fputs("\n]\n", out) != EOF;
    if (fclose(out) == 0 && written && rename(tmp_path, path) == 0)
        result = 0;
    else
        unlink(tmp_path);

done:
    if (in)
        fclose(in);
    return result;
}
