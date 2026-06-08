/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE

#include "api_multipart.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int extract_multipart_file(const char *boundary, const char *upload_path,
                           char *out_path, int out_sz,
                           char *filename, int fn_sz)
{
    int fd;
    struct stat st;
    char *data;
    char boundary_line[256];
    size_t blen;
    char *content_start = NULL;
    char *content_end = NULL;
    char *part;
    char *data_end;
    size_t content_len;
    int out_fd;
    size_t written = 0;

    fd = open(upload_path, O_RDONLY);
    if (fd < 0)
        return -1;

    if (fstat(fd, &st) < 0 || st.st_size == 0) {
        close(fd);
        return -1;
    }

    data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (data == MAP_FAILED)
        return -1;

    snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary);
    blen = strlen(boundary_line);
    filename[0] = '\0';
    part = data;
    data_end = data + st.st_size;

    while ((part = memmem(part, (size_t)(data_end - part),
                          boundary_line, blen)) != NULL) {
        char *headers_end;
        int header_sep_len = 4;
        size_t headers_len;
        int is_file_part = 0;
        char *headers;
        char *disp;
        char *part_content;
        char *next_boundary;

        part += blen;
        if (part + 2 <= data_end && part[0] == '-' && part[1] == '-')
            break;

        if (part + 2 <= data_end && part[0] == '\r' && part[1] == '\n')
            part += 2;
        else if (part < data_end && part[0] == '\n')
            part += 1;

        headers_end = memmem(part, (size_t)(data_end - part), "\r\n\r\n", 4);
        if (!headers_end) {
            headers_end = memmem(part, (size_t)(data_end - part), "\n\n", 2);
            header_sep_len = 2;
        }
        if (!headers_end)
            break;

        headers_len = (size_t)(headers_end - part);
        headers = malloc(headers_len + 1);
        if (!headers)
            break;
        memcpy(headers, part, headers_len);
        headers[headers_len] = '\0';

        disp = strcasestr(headers, "content-disposition:");
        if (disp) {
            char *line_end = strstr(disp, "\r\n");
            char *fn;

            if (!line_end)
                line_end = strchr(disp, '\n');
            if (line_end)
                *line_end = '\0';

            if (strcasestr(disp, "filename=\"") ||
                strcasestr(disp, "name=\"file\"") ||
                strcasestr(disp, "name=file"))
                is_file_part = 1;

            fn = strcasestr(disp, "filename=\"");
            if (fn) {
                char *fn_end;
                size_t copy_len;

                fn += 10;
                fn_end = strchr(fn, '"');
                if (fn_end) {
                    copy_len = (size_t)(fn_end - fn);
                    if (copy_len >= (size_t)fn_sz)
                        copy_len = (size_t)fn_sz - 1;
                    memcpy(filename, fn, copy_len);
                    filename[copy_len] = '\0';
                }
            } else {
                fn = strcasestr(disp, "filename=");
                if (fn) {
                    char *fn_end;
                    size_t copy_len;

                    fn += 9;
                    fn_end = fn;
                    while (*fn_end && *fn_end != ';' && *fn_end != ' ' &&
                           *fn_end != '\t')
                        fn_end++;
                    copy_len = (size_t)(fn_end - fn);
                    if (copy_len >= (size_t)fn_sz)
                        copy_len = (size_t)fn_sz - 1;
                    memcpy(filename, fn, copy_len);
                    filename[copy_len] = '\0';
                }
            }
        }
        free(headers);

        part_content = headers_end + header_sep_len;
        next_boundary = memmem(part_content,
                               (size_t)(data_end - part_content),
                               boundary_line, blen);
        if (!next_boundary)
            break;

        if (is_file_part) {
            content_start = part_content;
            content_end = next_boundary;
            break;
        }

        part = next_boundary;
    }

    if (!content_start || !content_end) {
        munmap(data, (size_t)st.st_size);
        return -1;
    }

    if (content_end > content_start && content_end[-1] == '\n')
        content_end--;
    if (content_end > content_start && content_end[-1] == '\r')
        content_end--;

    content_len = (size_t)(content_end - content_start);
    snprintf(out_path, (size_t)out_sz, "/tmp/deneb-gcode-XXXXXX");
    out_fd = mkstemp(out_path);
    if (out_fd < 0) {
        munmap(data, (size_t)st.st_size);
        return -1;
    }

    while (written < content_len) {
        size_t to_write = content_len - written;
        ssize_t nw;

        if (to_write > 65536)
            to_write = 65536;
        nw = write(out_fd, content_start + written, to_write);
        if (nw < 0) {
            close(out_fd);
            unlink(out_path);
            munmap(data, (size_t)st.st_size);
            return -1;
        }
        written += (size_t)nw;
    }

    close(out_fd);
    munmap(data, (size_t)st.st_size);
    return 0;
}
