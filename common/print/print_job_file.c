/* SPDX-License-Identifier: MPL-2.0 */
#include "print_job_file.h"

#include "print_state_rules.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void deneb_print_job_file_metadata_init(deneb_print_job_file_metadata_t *meta)
{
    if (!meta)
        return;
    memset(meta, 0, sizeof(*meta));
}

void deneb_print_job_start_plan_init(deneb_print_job_start_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    plan->source = DENEB_PRINT_DEFAULT_JOB_SOURCE;
    plan->uuid = DENEB_PRINT_DEFAULT_JOB_UUID;
}

int deneb_print_job_start_plan_prepare(const char *path, const char *source,
                                       const char *uuid, float bed_target,
                                       float nozzle_target,
                                       deneb_print_job_start_plan_t *plan)
{
    if (!plan || !path || !path[0]) {
        errno = EINVAL;
        return -1;
    }

    deneb_print_job_start_plan_init(plan);
    plan->path = path;
    plan->source = deneb_print_job_source_or_default(source);
    plan->uuid = deneb_print_job_uuid_or_default(uuid);
    plan->bed_target = bed_target;
    plan->nozzle_target = nozzle_target;
    return 0;
}

int deneb_print_job_start_plan_file(const char *path, const char *source,
                                    deneb_print_job_start_plan_t *plan)
{
    return deneb_print_job_start_plan_prepare(path, source, NULL, 0.0f, 0.0f,
                                             plan);
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

int deneb_print_job_file_sanitize_name(const char *name, char *out,
                                       size_t out_sz)
{
    const char *base;
    const char *slash;
    const char *backslash;

    if (!out || out_sz == 0)
        return -1;

    base = name && *name ? name : "upload.gcode";
    slash = strrchr(base, '/');
    backslash = strrchr(base, '\\');
    if (slash && (!backslash || slash > backslash))
        base = slash + 1;
    else if (backslash)
        base = backslash + 1;

    if (!*base || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
        base = "upload.gcode";

    if (strlen(base) >= out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }
    memmove(out, base, strlen(base) + 1);

    return 0;
}

int deneb_print_job_file_spool_path(const char *name, char *out,
                                    size_t out_sz)
{
    char safe[128];

    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (deneb_print_job_file_sanitize_name(name, safe, sizeof(safe)) < 0)
        return -1;

    if (snprintf(out, out_sz, "%s/%s", DENEB_PRINT_JOB_SPOOL_DIR,
                 safe) >= (int)out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }

    return 0;
}

static int copy_file(const char *src_path, const char *dest_path)
{
    int src_fd;
    int dst_fd;
    char buf[65536];
    ssize_t nr;
    int copy_ok = 1;

    src_fd = open(src_path, O_RDONLY);
    dst_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (src_fd < 0 || dst_fd < 0) {
        if (src_fd >= 0)
            close(src_fd);
        if (dst_fd >= 0)
            close(dst_fd);
        return -1;
    }

    while ((nr = read(src_fd, buf, sizeof(buf))) >= 0) {
        if (nr == 0)
            break;
        if (write(dst_fd, buf, (size_t)nr) != nr) {
            copy_ok = 0;
            break;
        }
    }
    if (nr < 0)
        copy_ok = 0;

    close(src_fd);
    close(dst_fd);
    return copy_ok ? 0 : -1;
}

int deneb_print_job_file_store_upload(const char *src_path,
                                      const char *dest_path)
{
    if (!src_path || !*src_path || !dest_path || !*dest_path) {
        errno = EINVAL;
        return -1;
    }

    if (mkdir(DENEB_PRINT_JOB_SPOOL_DIR, 0755) < 0 && errno != EEXIST)
        return -1;

    if (rename(src_path, dest_path) == 0)
        return 0;

    if (copy_file(src_path, dest_path) < 0) {
        unlink(dest_path);
        return -1;
    }

    unlink(src_path);
    return 0;
}
