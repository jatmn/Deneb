/* SPDX-License-Identifier: MPL-2.0 */
#include "json_file.h"

#include <stdio.h>
#include <string.h>

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void deneb_json_file_read_array_or_empty(const char *path,
                                         char *out,
                                         size_t out_sz)
{
    FILE *f;
    size_t n;
    int truncated;
    const char *start;

    if (!out || out_sz == 0)
        return;

    snprintf(out, out_sz, "[]");
    if (!path || !*path)
        return;

    f = fopen(path, "rb");
    if (!f)
        return;

    n = fread(out, 1, out_sz - 1, f);
    truncated = !feof(f);
    fclose(f);
    out[n] = '\0';

    start = out;
    while (*start && is_space((unsigned char)*start))
        start++;

    if (truncated || *start != '[' || !strrchr(start, ']'))
        snprintf(out, out_sz, "[]");
}
