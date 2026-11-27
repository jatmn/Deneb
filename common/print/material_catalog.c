/* SPDX-License-Identifier: MPL-2.0 */
#include "material_catalog.h"
#include "file_write.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int deneb_material_catalog_file_is_candidate(const char *name)
{
    const char *dot;

    if (!name || !*name)
        return 0;

    dot = strrchr(name, '.');
    if (!dot)
        return 0;

    return strcmp(dot, ".xml") == 0 ||
           strcmp(dot, ".fdm_material") == 0 ||
           strcmp(dot, ".material") == 0;
}

int deneb_material_catalog_copy_tag_value(const char *xml, const char *tag,
                                          char *out, size_t out_sz)
{
    char open_tag[32];
    char close_tag[32];
    const char *start;
    const char *end;
    size_t len;

    if (!xml || !tag || !out || out_sz == 0)
        return -1;

    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    start = strstr(xml, open_tag);
    if (!start)
        return -1;
    start += strlen(open_tag);
    end = strstr(start, close_tag);
    if (!end || end <= start)
        return -1;

    len = (size_t)(end - start);
    while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)start[len - 1]))
        len--;
    if (len == 0 || len >= out_sz)
        return -1;

    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

int deneb_material_catalog_guid_is_safe(const char *guid)
{
    size_t len;
    const char *p;

    if (!guid)
        return 0;
    len = strlen(guid);
    if (len < 32 || len >= 64)
        return 0;
    for (p = guid; *p; p++) {
        if (!(isxdigit((unsigned char)*p) || *p == '-'))
            return 0;
    }
    return 1;
}

static int parse_nonnegative_int(const char *value, int *out)
{
    long parsed;
    char *end;

    if (!value || !*value || !out)
        return -1;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < 0 || parsed > 2147483647L)
        return -1;

    *out = (int)parsed;
    return 0;
}

typedef struct {
    const char *open_tag;
    const char *close_tag;
    char *value;
    size_t capacity;
    size_t matched;
    size_t length;
    size_t trimmed;
    int opened;
    int done;
} material_tag_t;

static int scan_material_tag(material_tag_t *tag, unsigned char ch)
{
    if (tag->done)
        return 0;

    if (!tag->opened) {
        if (ch == (unsigned char)tag->open_tag[tag->matched]) {
            if (tag->open_tag[++tag->matched] == '\0') {
                tag->opened = 1;
                tag->matched = 0;
            }
        } else {
            /* The only possible overlapping prefix is another '<'. */
            tag->matched = ch == '<';
        }
        return 0;
    }

    if (tag->matched || ch == '<') {
        /* Neither field accepts '<' within its value. */
        if (ch != (unsigned char)tag->close_tag[tag->matched])
            return -1;
        if (tag->close_tag[++tag->matched] == '\0') {
            if (tag->trimmed == 0)
                return -1;
            tag->value[tag->trimmed] = '\0';
            tag->done = 1;
        }
        return 0;
    }

    int whitespace = isspace(ch);
    if (!tag->length && whitespace)
        return 0;
    if (tag->length < tag->capacity - 1)
        tag->value[tag->length] = (char)ch;
    ++tag->length;
    if (!whitespace) {
        if (tag->length >= tag->capacity)
            return -1;
        tag->trimmed = tag->length;
    }
    return 0;
}

int deneb_material_catalog_parse_file(const char *path,
                                      char *guid, size_t guid_sz,
                                      int *version)
{
    unsigned char chunk[512];
    size_t remaining = 128 * 1024;
    char guid_value[64];
    char version_str[32];
    material_tag_t guid_tag = {
        .open_tag = "<GUID>", .close_tag = "</GUID>", .value = guid_value,
        .capacity = guid_sz < sizeof(guid_value) ? guid_sz : sizeof(guid_value)
    };
    material_tag_t version_tag = {
        .open_tag = "<version>", .close_tag = "</version>",
        .value = version_str, .capacity = sizeof(version_str)
    };
    int result = -1;

    if (!path || !guid || guid_sz == 0 || !version)
        return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    while (remaining > 0) {
        size_t request = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        ssize_t n = read(fd, chunk, request);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        remaining -= (size_t)n;

        for (ssize_t i = 0; i < n; ++i) {
            if (chunk[i] == '\0' ||
                scan_material_tag(&guid_tag, chunk[i]) < 0 ||
                scan_material_tag(&version_tag, chunk[i]) < 0)
                goto done;
            if (guid_tag.done && version_tag.done) {
                if (deneb_material_catalog_guid_is_safe(guid_value) &&
                    parse_nonnegative_int(version_str, version) == 0) {
                    memcpy(guid, guid_value, guid_tag.trimmed + 1);
                    result = 0;
                }
                goto done;
            }
        }
    }

done:
    close(fd);
    return result;
}

int deneb_material_catalog_store_file(const char *path,
                                      const char *catalog_dir,
                                      char *guid, size_t guid_sz,
                                      int *version)
{
    char record_path[256];
    FILE *out;
    int n;

    if (!catalog_dir || !catalog_dir[0])
        return -1;
    if (deneb_material_catalog_parse_file(path, guid, guid_sz, version) < 0)
        return -1;

    if (mkdir(catalog_dir, 0755) < 0 && errno != EEXIST)
        return -1;

    n = snprintf(record_path, sizeof(record_path), "%s/%s.json",
                 catalog_dir, guid);
    if (n < 0 || (size_t)n >= sizeof(record_path))
        return -1;

    out = deneb_fopen_write(record_path, 0644);
    if (!out)
        return -1;
    fprintf(out, "{\"guid\":\"%s\",\"version\":%d}", guid, *version);
    if (fclose(out) != 0)
        return -1;
    return 0;
}

int deneb_material_catalog_store_uploaded_file_to_dir(const char *path,
                                                      const char *catalog_dir,
                                                      char *guid,
                                                      size_t guid_sz,
                                                      int *version)
{
    int rc;

    if (!path || !path[0])
        return -1;

    rc = deneb_material_catalog_store_file(path, catalog_dir, guid, guid_sz,
                                           version);
    unlink(path);
    return rc;
}

int deneb_material_catalog_store_uploaded_file(const char *path,
                                               char *guid, size_t guid_sz,
                                               int *version)
{
    return deneb_material_catalog_store_uploaded_file_to_dir(
        path, DENEB_MATERIAL_CATALOG_DIR, guid, guid_sz, version);
}

int deneb_material_catalog_import_tree(const char *root,
                                       const char *catalog_dir,
                                       int max_depth,
                                       int *imported)
{
    DIR *dir;
    struct dirent *ent;

    if (!root || !catalog_dir || !imported || max_depth < 0)
        return -1;

    dir = opendir(root);
    if (!dir)
        return -1;

    while ((ent = readdir(dir)) != NULL) {
        char path[512];
        struct stat st;

        if (ent->d_name[0] == '.')
            continue;

        if (snprintf(path, sizeof(path), "%s/%s", root, ent->d_name) >=
            (int)sizeof(path))
            continue;
        if (stat(path, &st) < 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (max_depth > 0) {
                deneb_material_catalog_import_tree(path, catalog_dir,
                                                   max_depth - 1, imported);
            }
        } else if (S_ISREG(st.st_mode) &&
                   deneb_material_catalog_file_is_candidate(ent->d_name)) {
            char guid[64];
            int version = 0;

            if (deneb_material_catalog_store_file(path, catalog_dir,
                                                  guid, sizeof(guid),
                                                  &version) == 0) {
                (*imported)++;
            }
        }
    }

    closedir(dir);
    return 0;
}

static int append_str(char **buf, size_t *cap, size_t *pos, const char *s)
{
    size_t len = strlen(s);

    while (*pos + len + 1 > *cap) {
        size_t new_cap = *cap ? (*cap * 2) : 256;
        char *new_buf;

        while (new_cap < *pos + len + 1)
            new_cap *= 2;

        new_buf = realloc(*buf, new_cap);
        if (!new_buf)
            return -1;

        *buf = new_buf;
        *cap = new_cap;
    }

    memcpy(*buf + *pos, s, len);
    *pos += len;
    (*buf)[*pos] = '\0';
    return 0;
}

int deneb_material_catalog_build_response(const char *stock_json,
                                          const char *catalog_dir,
                                          char **out,
                                          size_t *out_len)
{
    const size_t stock_len = stock_json ? strlen(stock_json) : 0;
    size_t cap = stock_len + 1024;
    size_t pos = stock_len > 0 ? stock_len - 1 : 0;
    char *body;
    DIR *dir;

    if (!out || !out_len || !stock_json || stock_len == 0)
        return -1;

    body = malloc(cap);
    if (!body)
        return -1;
    memcpy(body, stock_json, pos);
    body[pos] = '\0';

    dir = opendir(catalog_dir);
    if (dir) {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {
            char path[256];
            char record[160];
            FILE *f;
            size_t n;

            if (ent->d_name[0] == '.')
                continue;
            if (!strstr(ent->d_name, ".json"))
                continue;
            n = (size_t)snprintf(path, sizeof(path), "%s/%s",
                                 catalog_dir, ent->d_name);
            if (n >= sizeof(path))
                continue;
            f = fopen(path, "r");
            if (!f)
                continue;

            n = fread(record, 1, sizeof(record) - 1, f);
            fclose(f);
            record[n] = '\0';
            if (n == 0 || record[0] != '{')
                continue;

            if (append_str(&body, &cap, &pos, ",") < 0 ||
                append_str(&body, &cap, &pos, record) < 0) {
                closedir(dir);
                free(body);
                return -1;
            }
        }
        closedir(dir);
    }

    if (append_str(&body, &cap, &pos, "]") < 0) {
        free(body);
        return -1;
    }

    *out = body;
    *out_len = pos;
    return 0;
}
