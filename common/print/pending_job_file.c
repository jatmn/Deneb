/* SPDX-License-Identifier: MPL-2.0 */
#include "pending_job_file.h"
#include "command_format.h"
#include "json_field.h"
#include "json_file.h"
#include "print_state_rules.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

void deneb_pending_job_file_init(deneb_pending_job_file_t *job)
{
    if (!job)
        return;
    memset(job, 0, sizeof(*job));
    job->tracker = -1;
}

void deneb_pending_job_action_plan_init(deneb_pending_job_action_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    plan->kind = DENEB_PENDING_JOB_ACTION_NONE;
    plan->tracker = -1;
}

void deneb_pending_job_upload_check_init(deneb_pending_job_upload_check_t *check)
{
    if (!check)
        return;
    memset(check, 0, sizeof(*check));
    check->status = DENEB_PENDING_JOB_UPLOAD_CLEAR;
    check->tracker = -1;
}

void deneb_pending_job_conflict_prompt_init(
    deneb_pending_job_conflict_prompt_t *prompt)
{
    if (!prompt)
        return;
    memset(prompt, 0, sizeof(*prompt));
    snprintf(prompt->job_name, sizeof(prompt->job_name), "%s",
             "network print");
    snprintf(prompt->loaded_name, sizeof(prompt->loaded_name), "%s",
             "loaded material");
    snprintf(prompt->target_name, sizeof(prompt->target_name), "%s",
             "sliced material");
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

int deneb_pending_job_file_read_default_raw_array(char *out,
                                                  size_t out_sz,
                                                  size_t *out_len)
{
    return deneb_pending_job_file_read_raw_array(DENEB_PENDING_JOB_PATH, out,
                                                out_sz, out_len);
}

void deneb_pending_job_file_read_default_array_or_empty(char *out,
                                                        size_t out_sz)
{
    deneb_json_file_read_array_or_empty(DENEB_PENDING_JOB_PATH, out, out_sz);
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
    deneb_json_get_value(buf, "uuid", job->uuid, sizeof(job->uuid));
    deneb_json_get_value(buf, "source", job->source, sizeof(job->source));
    deneb_json_get_value(buf, "cloud_job_id", job->cloud_job_id,
                         sizeof(job->cloud_job_id));
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

int deneb_pending_job_file_load_pending_default(deneb_pending_job_file_t *job)
{
    return deneb_pending_job_file_load_default(job) == 0 &&
           deneb_pending_job_file_is_pending(job) ? 0 : -1;
}

int deneb_pending_job_file_load_conflict_default(deneb_pending_job_file_t *job)
{
    return deneb_pending_job_file_load_default(job) == 0 &&
           deneb_pending_job_file_has_conflict(job) ? 0 : -1;
}

int deneb_pending_job_file_has_pending_default(void)
{
    deneb_pending_job_file_t job;
    return deneb_pending_job_file_load_pending_default(&job) == 0;
}

int deneb_pending_job_file_has_conflict_default(void)
{
    deneb_pending_job_file_t job;
    return deneb_pending_job_file_load_conflict_default(&job) == 0;
}

int deneb_pending_job_file_plan_action(const deneb_pending_job_file_t *job,
                                       const char *instruction,
                                       deneb_pending_job_action_plan_t *plan)
{
    if (!job || !instruction || !plan || !deneb_pending_job_file_is_pending(job))
        return -1;

    deneb_pending_job_action_plan_init(plan);
    plan->tracker = job->tracker;

    if (strcmp(instruction, DENEB_PRINT_REQ_PREPARE) == 0) {
        if (!job->path[0])
            return -1;
        plan->kind = DENEB_PENDING_JOB_ACTION_START;
        snprintf(plan->command, sizeof(plan->command), "%s",
                 DENEB_COMMAND_VERB_JOB);
        snprintf(plan->path, sizeof(plan->path), "%s", job->path);
        snprintf(plan->source, sizeof(plan->source), "%s",
                 deneb_print_job_source_or_default(job->source));
        snprintf(plan->uuid, sizeof(plan->uuid), "%s",
                 deneb_print_job_uuid_or_default(job->uuid));
        snprintf(plan->cloud_job_id, sizeof(plan->cloud_job_id), "%s",
                 job->cloud_job_id);
        plan->mark_handled_after_success = 1;
        return 0;
    }

    if (strcmp(instruction, DENEB_COMMAND_VERB_ABORT) == 0) {
        plan->kind = DENEB_PENDING_JOB_ACTION_ABORT;
        snprintf(plan->command, sizeof(plan->command), "%s",
                 DENEB_COMMAND_VERB_ABORT);
        plan->clear_after_success = 1;
        return 0;
    }

    return -1;
}

int deneb_pending_job_file_finish_action(
    const char *path,
    const deneb_pending_job_action_plan_t *plan)
{
    const char *target_path = path && path[0] ? path : DENEB_PENDING_JOB_PATH;

    if (!plan)
        return -1;

    if (plan->mark_handled_after_success &&
        deneb_pending_job_file_mark_handled(target_path) < 0) {
        return deneb_pending_job_file_clear(target_path);
    }

    if (plan->clear_after_success)
        return deneb_pending_job_file_clear(target_path);

    return 0;
}

int deneb_pending_job_file_check_upload(const deneb_pending_job_file_t *job,
                                        const char *candidate_path,
                                        const char *fallback_name,
                                        deneb_pending_job_upload_check_t *check)
{
    if (!check)
        return -1;

    deneb_pending_job_upload_check_init(check);
    if (!deneb_pending_job_file_has_path(job))
        return 0;

    check->tracker = job->tracker;
    snprintf(check->path, sizeof(check->path), "%s", job->path);
    if (deneb_pending_job_file_display_name(job, check->display_name,
                                            sizeof(check->display_name)) != 0 &&
        fallback_name) {
        snprintf(check->display_name, sizeof(check->display_name), "%s",
                 fallback_name);
    }

    if (candidate_path &&
        deneb_pending_job_file_same_path(job->path, candidate_path)) {
        check->status = DENEB_PENDING_JOB_UPLOAD_DUPLICATE;
    } else {
        check->status = DENEB_PENDING_JOB_UPLOAD_BLOCKED;
    }

    return 0;
}

int deneb_pending_job_file_check_upload_default(
    const char *candidate_path,
    const char *fallback_name,
    deneb_pending_job_upload_check_t *check)
{
    deneb_pending_job_file_t job;

    if (!check)
        return -1;

    deneb_pending_job_upload_check_init(check);
    if (deneb_pending_job_file_load_pending_default(&job) != 0)
        return 0;

    return deneb_pending_job_file_check_upload(&job, candidate_path,
                                               fallback_name, check);
}

int deneb_pending_job_file_conflict_prompt(
    const deneb_pending_job_file_t *job,
    deneb_pending_job_conflict_prompt_t *prompt)
{
    if (!prompt)
        return -1;

    deneb_pending_job_conflict_prompt_init(prompt);
    if (!job)
        return -1;

    if (job->name[0])
        snprintf(prompt->job_name, sizeof(prompt->job_name), "%s",
                 job->name);
    if (job->origin_name[0])
        snprintf(prompt->loaded_name, sizeof(prompt->loaded_name), "%s",
                 job->origin_name);
    if (job->target_name[0])
        snprintf(prompt->target_name, sizeof(prompt->target_name), "%s",
                 job->target_name);

    prompt->is_pending = deneb_pending_job_file_is_pending(job);
    prompt->has_conflict = deneb_pending_job_file_has_conflict(job);
    return prompt->has_conflict ? 0 : -1;
}

int deneb_pending_job_file_conflict_prompt_default(
    deneb_pending_job_conflict_prompt_t *prompt)
{
    deneb_pending_job_file_t job;

    if (!prompt)
        return -1;
    deneb_pending_job_conflict_prompt_init(prompt);
    if (deneb_pending_job_file_load_default(&job) != 0)
        return -1;
    return deneb_pending_job_file_conflict_prompt(&job, prompt);
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
    return deneb_pending_job_file_is_pending(job) &&
           job->has_configuration_changes &&
           (job->has_material_change || job->has_print_core_change);
}

int deneb_pending_job_file_is_pending(const deneb_pending_job_file_t *job)
{
    return job && job->tracker >= 0;
}

int deneb_pending_job_file_has_path(const deneb_pending_job_file_t *job)
{
    return deneb_pending_job_file_is_pending(job) && job->path[0] != '\0';
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
    if (remove(path) == 0)
        return 0;
    return errno == ENOENT ? 0 : -1;
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
