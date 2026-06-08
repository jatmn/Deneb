/* SPDX-License-Identifier: MPL-2.0 */
#include "material_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

int deneb_material_catalog_parse_file(const char *path,
                                      char *guid, size_t guid_sz,
                                      int *version)
{
    FILE *f;
    char *xml;
    size_t n;
    char version_str[32];
    int ok;

    if (!path || !guid || guid_sz == 0 || !version)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;

    xml = malloc(131073);
    if (!xml) {
        fclose(f);
        return -1;
    }

    n = fread(xml, 1, 131072, f);
    fclose(f);
    xml[n] = '\0';

    ok = deneb_material_catalog_copy_tag_value(xml, "GUID", guid, guid_sz) == 0 &&
         deneb_material_catalog_copy_tag_value(xml, "version", version_str,
                                               sizeof(version_str)) == 0 &&
         deneb_material_catalog_guid_is_safe(guid);
    if (ok) {
        ok = parse_nonnegative_int(version_str, version) == 0;
    }

    free(xml);
    return ok ? 0 : -1;
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

    out = fopen(record_path, "w");
    if (!out)
        return -1;
    fprintf(out, "{\"guid\":\"%s\",\"version\":%d}", guid, *version);
    if (fclose(out) != 0)
        return -1;
    return 0;
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
