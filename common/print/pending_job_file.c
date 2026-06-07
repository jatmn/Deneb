/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job_file.h"
#include "json_field.h"
#include "print_state_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void deneb_pending_job_file_init(deneb_pending_job_file_t *job)
{
    if (!job)
        return;
    memset(job, 0, sizeof(*job));
    job->tracker = -1;
}

static int read_file(const char *path, char *buf, size_t buf_sz, size_t *out_len)
{
    FILE *f;
    size_t n;

    if (!path || !buf || buf_sz < 2)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;

    n = fread(buf, 1, buf_sz - 1, f);
    if (ferror(f) || !feof(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    if (n == 0)
        return -1;
    buf[n] = '\0';
    if (out_len)
        *out_len = n;
    return 0;
}

int deneb_pending_job_file_read_raw_array(const char *path,
                                          char *out,
                                          size_t out_sz,
                                          size_t *out_len)
{
    char *p;
    size_t n = 0;

    if (read_file(path, out, out_sz, &n) < 0)
        return -1;

    p = out;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (*p != '[')
        return -1;

    if (out_len)
        *out_len = n;
    return 0;
}

int deneb_pending_job_file_load(const char *path,
                                deneb_pending_job_file_t *job)
{
    char buf[8192];

    if (!job)
        return -1;

    deneb_pending_job_file_init(job);
    if (deneb_pending_job_file_read_raw_array(path, buf, sizeof(buf), NULL) < 0)
        return -1;

    deneb_json_get_value(buf, "path", job->path, sizeof(job->path));
    deneb_json_get_value(buf, "name", job->name, sizeof(job->name));
    deneb_json_get_value(buf, "origin_name", job->origin_name,
                         sizeof(job->origin_name));
    deneb_json_get_value(buf, "target_name", job->target_name,
                         sizeof(job->target_name));
    deneb_json_get_int_value(buf, "deneb_tracker", &job->tracker);
    job->has_configuration_changes =
        strstr(buf, "\"configuration_changes_required\"") != NULL;
    job->has_material_change = strstr(buf, "\"material_change\"") != NULL;
    job->has_print_core_change = strstr(buf, "\"print_core_change\"") != NULL;
    return 0;
}

int deneb_pending_job_file_load_default(deneb_pending_job_file_t *job)
{
    return deneb_pending_job_file_load(DENEB_PENDING_JOB_PATH, job);
}

int deneb_pending_job_file_display_value(const char *value, char *out, size_t out_sz)
{
    const char *base;
    const char *backslash;

    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (!value || !*value || strcmp(value, DENEB_PRINT_NONE_VALUE) == 0)
        return -1;

    base = strrchr(value, '/');
    backslash = strrchr(value, '\\');
    if (backslash && (!base || backslash > base))
        base = backslash;
    base = base ? base + 1 : value;
    if (!base || !*base || strcmp(base, DENEB_PRINT_NONE_VALUE) == 0)
        return -1;

    snprintf(out, out_sz, "%s", base);
    return out[0] ? 0 : -1;
}

int deneb_pending_job_file_display_name(const deneb_pending_job_file_t *job,
                                        char *out, size_t out_sz)
{
    if (!job || !out || out_sz == 0)
        return -1;

    if (deneb_pending_job_file_display_value(job->name, out, out_sz) == 0)
        return 0;
    return deneb_pending_job_file_display_value(job->path, out, out_sz);
}

int deneb_pending_job_file_default_display_name(char *out, size_t out_sz)
{
    deneb_pending_job_file_t job;

    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (deneb_pending_job_file_load_default(&job) != 0)
        return -1;

    return deneb_pending_job_file_display_name(&job, out, out_sz);
}

int deneb_pending_job_file_has_conflict(const deneb_pending_job_file_t *job)
{
    return job && job->tracker >= 0 && job->has_configuration_changes &&
           (job->has_material_change || job->has_print_core_change);
}

int deneb_pending_job_file_same_path(const char *pending_path,
                                     const char *candidate_path)
{
    const char *pp;
    const char *cp;

    if (!pending_path || !candidate_path)
        return 0;
    if (strcmp(pending_path, candidate_path) == 0)
        return 1;

    pp = strrchr(pending_path, '/');
    cp = strrchr(candidate_path, '/');
    pp = pp ? pp + 1 : pending_path;
    cp = cp ? cp + 1 : candidate_path;
    return strcmp(pp, cp) == 0;
}

int deneb_pending_job_file_clear(const char *path)
{
    if (!path || !*path)
        return -1;
    return remove(path);
}

int deneb_pending_job_file_clear_default(void)
{
    return deneb_pending_job_file_clear(DENEB_PENDING_JOB_PATH);
}

static int write_replaced(FILE *f, const char **p,
                          const char *old_text, const char *new_text)
{
    size_t old_len = strlen(old_text);
    size_t new_len = strlen(new_text);

    if (strncmp(*p, old_text, old_len) != 0)
        return 0;
    if (fwrite(new_text, 1, new_len, f) != new_len)
        return -1;
    *p += old_len;
    return 1;
}

int deneb_pending_job_file_mark_handled(const char *path)
{
    char buf[4096];
    FILE *f;
    const char *p;
    int wrote_change = 0;

    if (read_file(path, buf, sizeof(buf), NULL) < 0)
        return -1;

    if (!strstr(buf, "\"configuration_changes_required\"") &&
        !strstr(buf, "\"status\":\"wait_user_action\""))
        return 0;

    f = fopen(path, "wb");
    if (!f)
        return -1;

    p = buf;
    while (*p) {
        int r = write_replaced(f, &p,
            "\"configuration_changes_required\"",
            "\"configuration_changes_handled\"");
        if (r < 0) {
            fclose(f);
            return -1;
        }
        if (r > 0) {
            wrote_change = 1;
            continue;
        }

        r = write_replaced(f, &p,
            "\"status\":\"wait_user_action\"",
            "\"status\":\"" DENEB_PRINT_PHASE_NAME_PRE_PRINT "\"");
        if (r < 0) {
            fclose(f);
            return -1;
        }
        if (r > 0) {
            wrote_change = 1;
            continue;
        }

        if (fputc(*p++, f) == EOF) {
            fclose(f);
            return -1;
        }
    }

    if (fclose(f) != 0)
        return -1;
    return wrote_change ? 0 : -1;
}
