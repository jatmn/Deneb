/* SPDX-License-Identifier: MPL-2.0 */
#include "print_job_file.h"

#include "print_state_rules.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
    plan->cloud_job_id = "";
}

void deneb_print_job_upload_storage_plan_init(
    deneb_print_job_upload_storage_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
}

int deneb_print_job_start_plan_prepare(const char *path, const char *source,
                                       const char *uuid,
                                       const char *cloud_job_id,
                                       float bed_target,
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
    plan->cloud_job_id = cloud_job_id ? cloud_job_id : "";
    plan->bed_target = bed_target;
    plan->nozzle_target = nozzle_target;
    return 0;
}

int deneb_print_job_start_plan_file(const char *path, const char *source,
                                    deneb_print_job_start_plan_t *plan)
{
    return deneb_print_job_start_plan_prepare(path, source, NULL, NULL, 0.0f,
                                             0.0f, plan);
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

static int metadata_extract_positive_int(const char *buf,
                                         const char *key,
                                         int *out)
{
    const char *p;
    size_t key_len;

    if (!buf || !key || !out)
        return -1;

    key_len = strlen(key);
    for (p = strstr(buf, key); p; p = strstr(p + 1, key)) {
        const char *v = p + key_len;
        long value = 0;
        int digits = 0;

        if (*v && *v != ' ' && *v != '\t' && *v != ':' && *v != '=' &&
            *v != '"' && *v != '\'')
            continue;
        while (*v && (*v == ' ' || *v == '\t' || *v == ':' ||
                      *v == '=' || *v == '"' || *v == '\''))
            v++;
        while (*v >= '0' && *v <= '9') {
            value = value * 10 + (*v - '0');
            if (value > 2147483647L)
                return -1;
            v++;
            digits++;
        }
        if (digits > 0) {
            *out = (int)value;
            return 0;
        }
    }
    return -1;
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
    if (!meta->material_guid[0] &&
        deneb_print_job_file_metadata_extract_value(
            buf, "EXTRUDER_TRAIN.0.MATERIAL.GUID", meta->material_guid,
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
    if (!meta->nozzle_size[0] &&
        deneb_print_job_file_metadata_extract_value(
            buf, "EXTRUDER_TRAIN.0.NOZZLE.DIAMETER", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;
    if (metadata_extract_positive_int(buf, "PRINT.TIME",
                                      &meta->print_time_seconds) == 0)
        found = 1;
    else if (metadata_extract_positive_int(buf, "TIME",
                                           &meta->print_time_seconds) == 0)
        found = 1;

    return found ? 0 : -1;
}

int deneb_print_job_file_has_extension(const char *name, const char *extension)
{
    size_t name_len;
    size_t extension_len;

    if (!name || !extension)
        return 0;
    name_len = strlen(name);
    extension_len = strlen(extension);
    return name_len >= extension_len &&
           strcasecmp(name + name_len - extension_len, extension) == 0;
}

int deneb_print_job_file_replace_extension(const char *name,
                                           const char *extension,
                                           char *out,
                                           size_t out_sz)
{
    const char *dot;
    size_t base_len;

    if (!name || !*name || !extension || !out || out_sz == 0) {
        errno = EINVAL;
        return -1;
    }

    dot = strrchr(name, '.');
    base_len = dot ? (size_t)(dot - name) : strlen(name);
    if (base_len == 0 || base_len + strlen(extension) + 1 > out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }

    memcpy(out, name, base_len);
    out[base_len] = '\0';
    strncat(out, extension, out_sz - strlen(out) - 1);
    return 0;
}

static int extract_ufp_member(const char *ufp_path, const char *member,
                              const char *gcode_path)
{
    int out_fd;
    int status = 0;
    pid_t pid;
    struct stat st;

    out_fd = open(gcode_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(out_fd);
        return -1;
    }
    if (pid == 0) {
        if (dup2(out_fd, STDOUT_FILENO) < 0)
            _exit(126);
        close(out_fd);
        execlp("unzip", "unzip", "-p", ufp_path, member, (char *)NULL);
        _exit(127);
    }
    close(out_fd);

    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (stat(gcode_path, &st) == 0 && st.st_size > 0)
        return 0;
    unlink(gcode_path);
    if (!WIFEXITED(status))
        return -2;
    return WEXITSTATUS(status) != 0 ? WEXITSTATUS(status) : -3;
}

int deneb_print_job_file_extract_ufp_model_gcode(const char *ufp_path,
                                                 const char *gcode_path)
{
    int rc;

    if (!ufp_path || !*ufp_path || !gcode_path || !*gcode_path) {
        errno = EINVAL;
        return -1;
    }

    rc = extract_ufp_member(ufp_path, "3D/model.gcode", gcode_path);
    if (rc == 0)
        return 0;
    return extract_ufp_member(ufp_path, "/3D/model.gcode", gcode_path);
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

int deneb_print_job_file_upload_storage_plan(
    const char *filename,
    deneb_print_job_upload_storage_plan_t *plan)
{
    if (!plan) {
        errno = EINVAL;
        return -1;
    }

    deneb_print_job_upload_storage_plan_init(plan);
    if (deneb_print_job_file_sanitize_name(filename, plan->filename,
                                           sizeof(plan->filename)) < 0)
        return -1;
    return deneb_print_job_file_spool_path(plan->filename, plan->dest_path,
                                           sizeof(plan->dest_path));
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
