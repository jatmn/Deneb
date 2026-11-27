/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "print_history.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    FILE *file;
    off_t offset;
    int c;
    bool repair_utf8;
} history_input_t;

typedef enum {
    HISTORY_OK,
    HISTORY_MALFORMED,
    HISTORY_IO_FAILED
} history_status_t;

typedef struct {
    off_t start;
    off_t end;
} history_record_t;

typedef struct {
    off_t start;
    off_t end;
    uint64_t eligible_bytes;
    int has_oversized_record;
    bool repair_utf8;
} history_layout_t;

typedef struct {
    FILE *file;
    char *buffer;
    size_t capacity;
    size_t length;
} history_output_t;

static inline void advance(history_input_t *in)
{
    in->offset++;
    in->c = getc_unlocked(in->file);
}

static void skip_space(history_input_t *in)
{
    while (in->c == ' ' || in->c == '\t' || in->c == '\r' || in->c == '\n')
        advance(in);
}

static int digit(int c)
{
    return c >= '0' && c <= '9';
}

static int scan_digits(history_input_t *in)
{
    if (!digit(in->c))
        return -1;
    do {
        advance(in);
    } while (digit(in->c));
    return 0;
}

static int scan_number(history_input_t *in)
{
    if (in->c == '-')
        advance(in);
    if (in->c == '0')
        advance(in);
    else if (scan_digits(in) != 0)
        return -1;
    if (in->c == '.') {
        advance(in);
        if (scan_digits(in) != 0)
            return -1;
    }
    if (in->c == 'e' || in->c == 'E') {
        advance(in);
        if (in->c == '+' || in->c == '-')
            advance(in);
        if (scan_digits(in) != 0)
            return -1;
    }
    return 0;
}

static int utf8_trailing_bytes(int lead)
{
    if (lead < 0xc2 || lead > 0xf4)
        return 0;
    return lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
}

static int valid_utf8_trailing_byte(int c, int lead)
{
    int min = lead == 0xe0 ? 0xa0 : lead == 0xf0 ? 0x90 : 0x80;
    int max = lead == 0xed ? 0x9f : lead == 0xf4 ? 0x8f : 0xbf;
    return c >= min && c <= max;
}

static void scan_utf8(history_input_t *in)
{
    int lead = in->c;
    int count = utf8_trailing_bytes(lead);
    if (!count)
        in->repair_utf8 = true;
    while (count-- > 0) {
        advance(in);
        if (!valid_utf8_trailing_byte(in->c, lead)) {
            in->repair_utf8 = true;
            return;
        }
        lead = 0;
    }
    advance(in);
}

static int scan_string(history_input_t *in)
{
    if (in->c != '"')
        return -1;
    advance(in);
    while (in->c != '"') {
        if (in->c < ' ')
            return -1;
        if (in->c == '\\') {
            advance(in);
            if (in->c == 'u') {
                for (int i = 0; i < 4; i++) {
                    advance(in);
                    int c = in->c;
                    if (!digit(c) && !(c >= 'a' && c <= 'f') &&
                        !(c >= 'A' && c <= 'F'))
                        return -1;
                }
            } else if (in->c <= 0 || !strchr("\"\\/bfnrt", in->c)) {
                return -1;
            }
        } else if (in->c >= 0x80) {
            scan_utf8(in);
            continue;
        }
        advance(in);
    }
    advance(in);
    return 0;
}

static int scan_scalar(history_input_t *in)
{
    if (in->c == '"')
        return scan_string(in);
    if (in->c == '-' || digit(in->c))
        return scan_number(in);

    const char *literal;
    switch (in->c) {
    case 't': literal = "true"; break;
    case 'f': literal = "false"; break;
    case 'n': literal = "null"; break;
    default: return -1;
    }
    for (; *literal; literal++) {
        if (in->c != *literal)
            return -1;
        advance(in);
    }
    return 0;
}

static int scan_key(history_input_t *in)
{
    if (scan_string(in) != 0)
        return -1;
    skip_space(in);
    if (in->c != ':')
        return -1;
    advance(in);
    skip_space(in);
    return 0;
}

/* No record with this many nested pairs can fit, even without any values. */
static int skip_oversized_container(history_input_t *in)
{
    size_t depth = 1;
    advance(in);
    while (depth && in->c != EOF) {
        if (in->c == '"') {
            if (scan_string(in) != 0)
                return -1;
            continue;
        }
        if (in->c == '{' || in->c == '[')
            depth++;
        else if (in->c == '}' || in->c == ']')
            depth--;
        advance(in);
    }
    return depth ? -1 : 0;
}

/* One bit per nested container is enough to check every record that can fit. */
static int scan_record(history_input_t *in)
{
    if (in->c != '{')
        return -1;
    unsigned char arrays[(DENEB_PRINT_HISTORY_MAX_BYTES + 15) / 16];
    size_t depth = 0;
    for (;;) {
        if (in->c == '{' || in->c == '[') {
            int is_array = in->c == '[';
            if (depth == sizeof(arrays) * 8) {
                if (skip_oversized_container(in) != 0)
                    return -1;
            } else {
                size_t byte = depth / 8;
                unsigned int bit = 1u << (depth % 8);
                if (bit == 1)
                    arrays[byte] = 0;
                if (is_array)
                    arrays[byte] |= bit;
                else
                    arrays[byte] &= ~bit;
                depth++;
                advance(in);
                skip_space(in);
                if (in->c != (is_array ? ']' : '}')) {
                    if (!is_array && scan_key(in) != 0)
                        return -1;
                    continue;
                }
            }
        } else if (scan_scalar(in) != 0) {
            return -1;
        }

        for (;;) {
            skip_space(in);
            size_t level = depth - 1;
            int is_array = (arrays[level / 8] >> (level % 8)) & 1;
            if (in->c == (is_array ? ']' : '}')) {
                advance(in);
                if (--depth == 0)
                    return 0;
                continue;
            }
            if (in->c != ',')
                return -1;
            advance(in);
            skip_space(in);
            if (!is_array && scan_key(in) != 0)
                return -1;
            break;
        }
    }
}

static history_status_t start_history(FILE *file, history_input_t *in)
{
    if (fseeko(file, 0, SEEK_SET) != 0)
        return HISTORY_IO_FAILED;
    *in = (history_input_t){ .file = file, .c = getc_unlocked(file) };
    skip_space(in);
    if (in->c != '[')
        return ferror(file) ? HISTORY_IO_FAILED : HISTORY_MALFORMED;
    advance(in);
    skip_space(in);
    return HISTORY_OK;
}

/* Return one whole object, zero at the end of the array, or -1 on error. */
static int next_record(history_input_t *in, history_record_t *record)
{
    if (in->c == ']') {
        advance(in);
        skip_space(in);
        return in->c == EOF && !ferror(in->file) ? 0 : -1;
    }
    if (in->c != '{')
        return -1;
    record->start = in->offset;
    if (scan_record(in) != 0)
        return -1;
    record->end = in->offset;
    skip_space(in);
    if (in->c == ',') {
        advance(in);
        skip_space(in);
        return in->c == '{' ? 1 : -1;
    }
    return in->c == ']' ? 1 : -1;
}

static history_status_t inspect_history(FILE *file, size_t budget, history_layout_t *layout)
{
    history_input_t in;
    *layout = (history_layout_t){0};
    history_status_t status = start_history(file, &in);
    if (status != HISTORY_OK)
        return status;

    history_record_t record;
    int next;
    while ((next = next_record(&in, &record)) > 0) {
        off_t length = record.end - record.start;
        if (length > (off_t)budget) {
            layout->has_oversized_record = 1;
            continue;
        }
        if (layout->eligible_bytes == 0)
            layout->start = record.start;
        layout->end = record.end;
        layout->eligible_bytes += (uint64_t)length + 1;
    }
    if (next == 0) {
        layout->repair_utf8 = in.repair_utf8;
        return HISTORY_OK;
    }
    return ferror(file) ? HISTORY_IO_FAILED : HISTORY_MALFORMED;
}

/* Repair old byte-truncated names without changing the retained byte count.
 * Defer a partial character at a block boundary to the next read. */
static size_t repair_history_utf8(char *text, size_t length, bool last_block)
{
    size_t i = 0;
    while (i < length) {
        int lead = (unsigned char)text[i];
        if (lead < 0x80) {
            i++;
            continue;
        }
        size_t trailing = (size_t)utf8_trailing_bytes(lead);
        if (trailing >= length - i && !last_block)
            break;

        size_t j = 1;
        while (j <= trailing && j < length - i &&
               valid_utf8_trailing_byte((unsigned char)text[i + j], lead)) {
            lead = 0;
            j++;
        }
        if (trailing == 0 || j <= trailing)
            text[i++] = '?';
        else
            i += j;
    }
    return i;
}

static int read_history_chunk(int fd, char *buffer, size_t count, off_t offset)
{
    for (size_t received = 0; received < count;) {
        ssize_t n = pread(fd, buffer + received, count - received, offset + (off_t)received);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        received += (size_t)n;
    }
    return 0;
}

static int copy_slice(int fd, off_t offset, size_t length, bool repair_utf8,
                      history_output_t *out)
{
    if (length > out->capacity - out->length)
        return -1;
    char chunk[1024];
    while (length > 0) {
        char *dest = out->file ? chunk : out->buffer + out->length;
        size_t count = out->file && length > sizeof(chunk) ? sizeof(chunk) : length;
        if (read_history_chunk(fd, dest, count, offset) != 0)
            return -1;
        size_t copied = repair_utf8 ? repair_history_utf8(dest, count, count == length) : count;
        if (out->file && fwrite(dest, 1, copied, out->file) != copied)
            return -1;
        offset += (off_t)copied;
        out->length += copied;
        length -= copied;
    }
    return 0;
}

static int copy_contiguous_history(FILE *file, const history_layout_t *layout,
                                   history_output_t *out)
{
    off_t start = layout->start;
    off_t cutoff = layout->end - (off_t)out->capacity;
    if (start < cutoff) {
        history_input_t in;
        if (start_history(file, &in) != HISTORY_OK)
            return -1;
        while (in.c == '{' && in.offset < cutoff) {
            history_record_t record;
            if (next_record(&in, &record) <= 0)
                return -1;
        }
        if (in.c != '{')
            return -1;
        start = in.offset;
    }
    return copy_slice(fileno(file), start, (size_t)(layout->end - start),
                      layout->repair_utf8, out);
}

/* Individually oversized objects are omitted; use a comma to bridge each gap. */
static int copy_retained_history(FILE *file, const history_layout_t *layout,
                                 history_output_t *out)
{
    if (layout->eligible_bytes == 0)
        return 0;
    if (!layout->has_oversized_record)
        return copy_contiguous_history(file, layout, out);

    history_input_t in;
    if (start_history(file, &in) != HISTORY_OK)
        return -1;
    uint64_t eligible = layout->eligible_bytes;
    history_record_t record;
    int next;
    while ((next = next_record(&in, &record)) > 0) {
        off_t length = record.end - record.start;
        if (length > (off_t)out->capacity)
            continue;
        if (eligible > out->capacity + 1) {
            eligible -= (uint64_t)length + 1;
            continue;
        }
        if (out->length > 0) {
            if (out->length == out->capacity)
                return -1;
            if (out->file) {
                if (fputc(',', out->file) == EOF)
                    return -1;
            } else {
                out->buffer[out->length] = ',';
            }
            out->length++;
        }
        if (copy_slice(fileno(file), record.start, (size_t)length, layout->repair_utf8, out) != 0)
            return -1;
    }
    return next;
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
    history_output_t result = { .buffer = out + 1, .capacity = capacity - 2 };
    FILE *in = fopen(DENEB_PRINT_HISTORY_PATH, "rb");
    if (in) {
        history_layout_t layout;
        if (inspect_history(in, result.capacity, &layout) != HISTORY_OK ||
            copy_retained_history(in, &layout, &result) != 0)
            result.length = 0;
        fclose(in);
    }
    out[0] = '[';
    out[result.length + 1] = ']';
    out[result.length + 2] = '\0';
}

static void format_utc_time(long long unix_time, char *out, size_t out_sz)
{
    time_t t = (time_t)unix_time;
    struct tm utc;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';
    if (unix_time <= 0)
        return;

    if (!gmtime_r(&t, &utc))
        return;
    snprintf(out, out_sz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);
}

static size_t utf8_character_length(const unsigned char *text)
{
    int lead = text[0];
    int count = utf8_trailing_bytes(lead);
    if (!count)
        return 0;
    for (int i = 1; i <= count; i++) {
        if (!valid_utf8_trailing_byte(text[i], lead))
            return 0;
        lead = 0;
    }
    return (size_t)count + 1;
}

/* Status filenames may already have been truncated at a byte boundary. */
static void escape_history_string(const char *src, char *out, size_t capacity)
{
    const unsigned char *p = (const unsigned char *)src;
    size_t length = 0;
    while (p && *p && length < capacity - 1) {
        if (*p >= 0x80) {
            size_t count = utf8_character_length(p);
            if (count == 0) {
                out[length++] = '?';
                p++;
                continue;
            }
            if (count > capacity - length - 1)
                break;
            memcpy(out + length, p, count);
            length += count;
            p += count;
            continue;
        }
        if (*p == '"' || *p == '\\') {
            if (capacity - length < 3)
                break;
            out[length++] = '\\';
        }
        if (*p >= ' ')
            out[length++] = (char)*p;
        p++;
    }
    out[length] = '\0';
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

    if (!entry || !out || out_sz == 0 || !isfinite(entry->progress))
        return -1;

    escape_history_string(entry->name, name, sizeof(name));
    escape_history_string(entry->uuid, uuid, sizeof(uuid));
    escape_history_string(entry->source, source, sizeof(source));
    escape_history_string(entry->state, state, sizeof(state));
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
    history_layout_t layout = {0};
    struct stat previous;
    /* Reserve the new record, outer brackets, whitespace and the comma. */
    size_t budget = DENEB_PRINT_HISTORY_MAX_BYTES - strlen(entry_json) -
                    (sizeof("[\n,\n\n]\n") - 1);
    if (in) {
        if (fstat(fileno(in), &previous) != 0)
            goto done;
        history_status_t status = inspect_history(in, budget, &layout);
        if (status == HISTORY_IO_FAILED)
            goto done;
        if (status == HISTORY_MALFORMED)
            layout = (history_layout_t){0};
    }

    /* The API owner is the sole writer. Clear its unpublished file on retry. */
    if (unlink(tmp_path) != 0 && errno != ENOENT)
        goto done;
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0664);
    if (fd < 0)
        goto done;
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(tmp_path);
        goto done;
    }

    history_output_t output = { .file = out, .capacity = budget };
    int written = (!in || fchmod(fd, previous.st_mode & 0775) == 0) &&
                  fputs("[\n", out) != EOF &&
                  copy_retained_history(in, &layout, &output) == 0 &&
                  (!output.length || fputs(",\n", out) != EOF) &&
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
