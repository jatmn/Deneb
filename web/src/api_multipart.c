/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE

#include "api_http.h"
#include "api_multipart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MULTIPART_BUFFER_SIZE 8192

typedef struct {
    char data[MULTIPART_BUFFER_SIZE];
    size_t start;
    size_t end;
    off_t offset;
    bool eof;
} multipart_reader_t;

static int refill(multipart_reader_t *reader, int fd, const atomic_bool *cancelled)
{
    if (cancelled && atomic_load(cancelled)) {
        errno = ECANCELED;
        return -1;
    }
    reader->offset += (off_t)reader->start;
    reader->end -= reader->start;
    memmove(reader->data, reader->data + reader->start, reader->end);
    reader->start = 0;
    if (reader->end == sizeof(reader->data))
        return -1;

    ssize_t n;
    do {
        n = read(fd, reader->data + reader->end, sizeof(reader->data) - reader->end);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return -1;
    reader->end += (size_t)n;
    reader->eof = n == 0;
    return 0;
}

static char *read_line(multipart_reader_t *reader, int fd, const atomic_bool *cancelled)
{
    while (true) {
        char *line = reader->data + reader->start;
        char *nl = memchr(line, '\n', reader->end - reader->start);
        if (nl) {
            reader->start = (size_t)(nl - reader->data) + 1;
            if (nl > line && nl[-1] == '\r')
                nl--;
            *nl = '\0';
            return line;
        }
        if (reader->eof || refill(reader, fd, cancelled) < 0)
            return NULL;
    }
}

static void copy_filename(const char *disp, char *filename, size_t capacity)
{
    const char *start = strcasestr(disp, "filename=");
    if (!start)
        return;
    start += sizeof("filename=") - 1;
    const char *end = *start == '"' ? strchr(++start, '"') : start + strcspn(start, "; \t");
    if (!end)
        return;
    size_t length = (size_t)(end - start);
    if (length >= capacity)
        length = capacity - 1;
    memcpy(filename, start, length);
    filename[length] = '\0';
}

static bool disposition_has_name(const char *disp, const char *name)
{
    char quoted[160];
    char bare[160];
    snprintf(quoted, sizeof(quoted), "name=\"%s\"", name);
    snprintf(bare, sizeof(bare), "name=%s", name);
    return strcasestr(disp, quoted) || strcasestr(disp, bare);
}

/* The overlap keeps a possible delimiter, its suffix and its preceding CR. */
static int read_part_end(multipart_reader_t *reader, int fd, const char *marker,
                         size_t marker_len, off_t part_start, off_t *part_end,
                         const atomic_bool *cancelled)
{
    while (true) {
        char *line = memmem(reader->data + reader->start, reader->end - reader->start,
                            marker, marker_len);
        if (line) {
            char *suffix = line + marker_len;
            size_t suffix_len = reader->end - (size_t)(suffix - reader->data);
            if (suffix_len >= 4 || reader->eof) {
                size_t end_len = 0;
                if (suffix_len >= 1 && suffix[0] == '\n')
                    end_len = 1;
                if (suffix_len >= 2 && memcmp(suffix, "\r\n", 2) == 0)
                    end_len = 2;
                bool final = suffix_len >= 2 && memcmp(suffix, "--", 2) == 0 &&
                             (suffix_len == 2 || suffix[2] == '\n' ||
                              (suffix_len >= 4 && memcmp(suffix + 2, "\r\n", 2) == 0));
                if (end_len || final) {
                    *part_end = reader->offset + (off_t)(line - reader->data);
                    if (*part_end > part_start && line > reader->data && line[-1] == '\r')
                        (*part_end)--;
                    reader->start = (size_t)(suffix - reader->data) + end_len;
                    return final ? 1 : 0;
                }
                reader->start = (size_t)(line - reader->data) + 1;
                continue;
            }
        }

        if (reader->eof)
            return -1;
        size_t keep_from = reader->end > marker_len + 4 ? reader->end - marker_len - 4 : 0;
        if (keep_from > reader->start)
            reader->start = keep_from;
        if (refill(reader, fd, cancelled) < 0)
            return -1;
    }
}

static ssize_t read_at(int fd, void *data, size_t length, off_t offset)
{
    ssize_t n;
    do {
        n = pread(fd, data, length, offset);
    } while (n < 0 && errno == EINTR);
    return n;
}

int parse_multipart_upload(int fd, const char *boundary,
                           multipart_range_t *file, char *filename, size_t filename_capacity,
                           multipart_field_t *fields, size_t field_count,
                           const atomic_bool *cancelled)
{
    multipart_reader_t reader = {0};
    char marker[sizeof("\n--") + HTTP_MULTIPART_BOUNDARY_SIZE - 1];
    int marker_len = snprintf(marker, sizeof(marker), "\n--%s", boundary);
    if (marker_len <= 3 || (size_t)marker_len >= sizeof(marker))
        return -1;

    filename[0] = '\0';
    char *line;
    do {
        line = read_line(&reader, fd, cancelled);
        if (!line)
            return -1;
    } while (strcmp(line, marker + 1) != 0);

    bool found_file = false;
    size_t remaining_fields = field_count;
    while (true) {
        bool saw_disposition = false;
        bool is_file = false;
        multipart_field_t *field = NULL;
        while ((line = read_line(&reader, fd, cancelled)) != NULL && *line) {
            if (saw_disposition || strncasecmp(line, "content-disposition:", 20) != 0)
                continue;
            saw_disposition = true;
            if (!found_file &&
                (strcasestr(line, "filename=\"") || disposition_has_name(line, "file"))) {
                is_file = true;
                copy_filename(line, filename, filename_capacity);
            }
            if (!strcasestr(line, "filename=")) {
                for (size_t i = 0; i < field_count; i++) {
                    if (!fields[i].found && disposition_has_name(line, fields[i].name)) {
                        field = &fields[i];
                        break;
                    }
                }
            }
        }
        if (!line)
            return -1;

        off_t start = reader.offset + (off_t)reader.start;
        off_t end;
        int final = read_part_end(&reader, fd, marker, (size_t)marker_len, start, &end,
                                  cancelled);
        if (final < 0)
            return -1;
        if (is_file) {
            *file = (multipart_range_t){ .offset = start, .length = end - start };
            found_file = true;
        }
        if (field) {
            size_t size = (size_t)(end - start);
            if (size >= field->capacity)
                size = field->capacity - 1;
            if (read_at(fd, field->value, size, start) != (ssize_t)size)
                return -1;
            field->value[size] = '\0';
            field->found = true;
            remaining_fields--;
        }
        if (final || (found_file && remaining_fields == 0))
            return found_file ? 0 : -1;
    }
}

int copy_multipart_range(int input, int output, const multipart_range_t *range,
                         const atomic_bool *cancelled)
{
    char data[MULTIPART_BUFFER_SIZE];
    for (off_t done = 0; done < range->length;) {
        if (cancelled && atomic_load(cancelled)) {
            errno = ECANCELED;
            return -1;
        }
        off_t remaining = range->length - done;
        size_t size = remaining < (off_t)sizeof(data) ? (size_t)remaining : sizeof(data);
        ssize_t n = read_at(input, data, size, range->offset + done);
        if (n <= 0)
            return -1;
        for (ssize_t written = 0; written < n;) {
            ssize_t nw = write(output, data + written, (size_t)(n - written));
            if (nw < 0 && errno == EINTR)
                continue;
            if (nw <= 0)
                return -1;
            written += nw;
        }
        done += n;
    }
    return 0;
}

int extract_multipart_file(const char *boundary, const char *upload_path,
                           char *out_path, int out_sz,
                           char *filename, int fn_sz)
{
    int input = open(upload_path, O_RDONLY | O_CLOEXEC);
    if (input < 0)
        return -1;
    multipart_range_t file;
    int result = parse_multipart_upload(input, boundary, &file, filename, (size_t)fn_sz,
                                        NULL, 0, NULL);
    if (result == 0) {
        snprintf(out_path, (size_t)out_sz, DENEB_UPLOAD_TEMP_DIR "/deneb-gcode-XXXXXX");
        int output = mkostemp(out_path, O_CLOEXEC);
        if (output < 0) {
            result = -1;
        } else {
            result = copy_multipart_range(input, output, &file, NULL);
            if (close(output) < 0)
                result = -1;
            if (result < 0)
                unlink(out_path);
        }
    }
    close(input);
    return result;
}
