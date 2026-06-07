/* SPDX-License-Identifier: MPL-2.0 */
#include "print_job_file.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void deneb_print_job_file_metadata_init(deneb_print_job_file_metadata_t *meta)
{
    if (!meta)
        return;
    memset(meta, 0, sizeof(*meta));
}

int deneb_print_job_file_metadata_extract_value(const char *buf,
                                                const char *key,
                                                char *out,
                                                size_t out_sz)
{
    const char *p;
    size_t i = 0;

    if (!buf || !key || !out || out_sz == 0)
        return -1;

    p = strstr(buf, key);
    if (!p)
        return -1;
    out[0] = '\0';
    p += strlen(key);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':' || *p == '=' ||
                  *p == '"' || *p == '\''))
        p++;
    while (*p && *p != '"' && *p != '\'' && *p != ',' && *p != ';' &&
           *p != '\r' && *p != '\n' && !isspace((unsigned char)*p) &&
           i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0 ? 0 : -1;
}

int deneb_print_job_file_metadata_load(const char *path,
                                       deneb_print_job_file_metadata_t *meta)
{
    char buf[131073];
    FILE *f;
    size_t n;
    int found = 0;

    if (!path || !meta)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;

    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    if (deneb_print_job_file_metadata_extract_value(
            buf, "material_guid", meta->material_guid,
            sizeof(meta->material_guid)) == 0)
        found = 1;
    if (deneb_print_job_file_metadata_extract_value(
            buf, "nozzle_size", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;
    if (deneb_print_job_file_metadata_extract_value(
            buf, "print_core_id", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;

    return found ? 0 : -1;
}
