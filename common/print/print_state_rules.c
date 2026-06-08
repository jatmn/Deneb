/* SPDX-License-Identifier: MPL-2.0 */
#include "print_state_rules.h"
#include "command_format.h"
#include "print_macros.h"

#include <stddef.h>
#include <string.h>

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ascii_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int str_eq_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;

    while (*a && *b) {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int str_is_one_of_ci(const char *value, const char *const *choices)
{
    if (!value || !*value)
        return 0;

    for (int i = 0; choices[i]; i++) {
        if (str_eq_ci(value, choices[i]))
            return 1;
    }
    return 0;
}

static int str_contains_ci(const char *haystack, const char *needle)
{
    size_t hlen;
    size_t nlen;

    if (!haystack || !needle)
        return 0;

    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen)
        return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen &&
               ascii_tolower((unsigned char)haystack[i + j]) ==
                   ascii_tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen)
            return 1;
    }

    return 0;
}

int deneb_print_req_is_print(const char *req)
{
    static const char *const print_reqs[] = {
        DENEB_COMMAND_VERB_JOB, "Print", DENEB_PRINT_REQ_PRINTING,
        DENEB_COMMAND_VERB_PAUSE, "Pause", DENEB_PRINT_REQ_PAUSED,
        NULL
    };

    return str_is_one_of_ci(req, print_reqs);
}

int deneb_print_req_is_paused(const char *req)
{
    static const char *const paused_reqs[] = {
        DENEB_COMMAND_VERB_PAUSE, "Pause", DENEB_PRINT_REQ_PAUSED,
        NULL
    };

    return str_is_one_of_ci(req, paused_reqs);
}

int deneb_print_req_is_lifecycle(const char *req)
{
    static const char *const lifecycle_reqs[] = {
        "HOME", "HOMING", "HOME_AND_CENTER_HEAD",
        "RESOLVE_CONFLICTS", DENEB_PRINT_REQ_PREPARE,
        DENEB_PRINT_REQ_PREHEAT, DENEB_PRINT_REQ_PREHEATING,
        "BED_PREHEATING", "HEAT_BED", "BED_AND_NOZZLE_PREHEATING",
        "EXTRACT", "EXTRACTING",
        NULL
    };

    return str_is_one_of_ci(req, lifecycle_reqs);
}

int deneb_print_req_is_abort(const char *req)
{
    static const char *const abort_reqs[] = {
        DENEB_COMMAND_VERB_ABORT, "Abort", DENEB_PRINT_REQ_ABORTING, "ABORTING",
        "BUSY_ABORTING",
        NULL
    };

    return str_is_one_of_ci(req, abort_reqs);
}

int deneb_print_file_is_transient(const char *file)
{
    if (!file || !*file || strcmp(file, DENEB_PRINT_NONE_VALUE) == 0)
        return 0;

    if (str_contains_ci(file, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD))
        return 1;
    if (str_contains_ci(file, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_UP))
        return 1;
    if (str_contains_ci(file, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_DOWN))
        return 1;
    if (str_contains_ci(file, "macro") && str_contains_ci(file, ".gcode"))
        return 1;

    return 0;
}

int deneb_print_file_is_candidate(const char *file)
{
    if (!file || !*file || strcmp(file, DENEB_PRINT_NONE_VALUE) == 0)
        return 0;

    return (str_contains_ci(file, ".gcode") || str_contains_ci(file, ".ufp")) &&
           !deneb_print_file_is_transient(file);
}

int deneb_print_has_temp_targets(float bed_target, float nozzle_target)
{
    return bed_target > 0.0f || nozzle_target > 0.0f;
}

int deneb_print_temp_target_ready(float current, float target, float tolerance)
{
    if (target <= 0.0f)
        return 1;
    if (tolerance <= 0.0f)
        tolerance = 1.0f;
    return current >= target - tolerance;
}

int deneb_print_temp_targets_ready(float bed_current, float bed_target,
                                   float nozzle_current, float nozzle_target)
{
    if (!deneb_print_has_temp_targets(bed_target, nozzle_target))
        return 0;

    return deneb_print_temp_target_ready(bed_current, bed_target, 1.0f) &&
           deneb_print_temp_target_ready(nozzle_current, nozzle_target, 1.0f);
}

int deneb_print_material_move_ready(float current_nozzle_temp,
                                    float target_nozzle_temp)
{
    return target_nozzle_temp >= DENEB_PRINT_MATERIAL_MIN_MOVE_TEMP_C &&
           deneb_print_temp_target_ready(
               current_nozzle_temp, target_nozzle_temp,
               DENEB_PRINT_MATERIAL_READY_TOLERANCE_C);
}

int deneb_print_active_time(int time_total, int time_left)
{
    return time_total > 0 && time_left > 0 && time_left <= time_total;
}

void deneb_print_observation_init(deneb_print_observation_t *obs,
                                  const char *req,
                                  const char *file,
                                  int time_total,
                                  int time_left,
                                  float bed_target,
                                  float nozzle_target)
{
    if (!obs)
        return;

    obs->req = req;
    obs->file = file;
    obs->time_total = time_total;
    obs->time_left = time_left;
    obs->bed_target = bed_target;
    obs->nozzle_target = nozzle_target;
}

int deneb_print_observation_has_context(const deneb_print_observation_t *obs)
{
    int has_time;
    int has_file;

    if (!obs || deneb_print_req_is_abort(obs->req))
        return 0;

    has_time = obs->time_total > 0 || obs->time_left > 0;
    has_file = deneb_print_file_is_candidate(obs->file);

    if (has_time || has_file)
        return 1;

    if (!obs->req || !obs->req[0])
        return 0;

    if (deneb_print_req_is_lifecycle(obs->req))
        return deneb_print_has_temp_targets(obs->bed_target, obs->nozzle_target);

    return deneb_print_req_is_print(obs->req);
}

int deneb_print_has_active_context(const deneb_print_observation_t *obs,
                                   int is_printing, int is_paused,
                                   int has_print_name)
{
    if (!obs || deneb_print_req_is_abort(obs->req))
        return 0;

    return is_printing || is_paused || has_print_name ||
           deneb_print_observation_has_context(obs);
}

int deneb_print_has_preparing_context(const deneb_print_observation_t *obs,
                                      int has_print_name)
{
    if (!obs || deneb_print_req_is_abort(obs->req) || !has_print_name)
        return 0;

    return deneb_print_req_is_lifecycle(obs->req) ||
           deneb_print_has_temp_targets(obs->bed_target, obs->nozzle_target) ||
           deneb_print_req_is_print(obs->req) ||
           deneb_print_req_is_paused(obs->req);
}

int deneb_print_has_stoppable_context(const deneb_print_observation_t *obs,
                                      int is_printing, int is_paused,
                                      int has_print_name)
{
    if (!obs || deneb_print_req_is_abort(obs->req))
        return 0;

    if (is_paused)
        return 1;

    if (!has_print_name)
        return 0;

    if (is_printing || obs->time_total > 0 || obs->time_left > 0)
        return 1;

    return deneb_print_req_is_print(obs->req) ||
           deneb_print_req_is_paused(obs->req) ||
           deneb_print_req_is_lifecycle(obs->req) ||
           deneb_print_has_temp_targets(obs->bed_target, obs->nozzle_target);
}

const char *deneb_print_status_label(int connected, int has_error,
                                     int is_paused, int is_active)
{
    if (has_error)
        return "error";
    if (is_paused)
        return "paused";
    if (is_active)
        return "printing";
    if (!connected)
        return "offline";
    return "idle";
}

const char *deneb_print_job_status_label(int has_error, int is_paused,
                                         int is_active)
{
    if (has_error)
        return "error";
    if (is_paused)
        return "paused";
    if (is_active)
        return "printing";
    return "finished";
}

const char *deneb_print_job_state_or_none(int has_error, int is_paused,
                                          int is_active)
{
    if (!deneb_print_job_is_active(has_error, is_paused, is_active))
        return DENEB_PRINT_NONE_VALUE;
    return deneb_print_job_status_label(has_error, is_paused, is_active);
}

const char *deneb_print_job_name_or_default(const char *name)
{
    if (!name || !name[0] || strcmp(name, DENEB_PRINT_NONE_VALUE) == 0)
        return DENEB_PRINT_DEFAULT_JOB_NAME;
    return name;
}

const char *deneb_print_job_uuid_or_default(const char *uuid)
{
    if (!uuid || !uuid[0] || strcmp(uuid, DENEB_PRINT_NONE_VALUE) == 0)
        return DENEB_PRINT_DEFAULT_JOB_UUID;
    return uuid;
}

const char *deneb_print_job_source_or_default(const char *source)
{
    if (!source || !source[0] || strcmp(source, DENEB_PRINT_NONE_VALUE) == 0)
        return DENEB_PRINT_DEFAULT_JOB_SOURCE;
    return source;
}

const char *deneb_print_completion_state_label(int has_error, int time_total,
                                               int time_left)
{
    if (has_error)
        return "error";
    if (time_total > 0 && time_left <= 0)
        return "completed";
    return "stopped";
}

int deneb_print_job_is_active(int has_error, int is_paused, int is_active)
{
    return has_error || is_paused || is_active;
}

int deneb_print_start_allowed(int connected, int has_error,
                              int is_paused, int is_active)
{
    return connected && !has_error && !is_paused && !is_active;
}

int deneb_print_manual_action_allowed(int connected, int has_error,
                                      int is_paused, int is_active)
{
    return deneb_print_start_allowed(connected, has_error, is_paused,
                                     is_active);
}

deneb_print_display_state_t deneb_print_display_state(
    int connected,
    int has_error,
    int is_paused,
    int is_printing,
    int has_abort_context,
    int has_preparing_context,
    int time_total)
{
    if (!connected)
        return DENEB_PRINT_DISPLAY_STATE_PREPARING;
    if (has_error)
        return DENEB_PRINT_DISPLAY_STATE_ERROR;
    if (has_abort_context)
        return DENEB_PRINT_DISPLAY_STATE_COOLING;
    if (is_paused)
        return DENEB_PRINT_DISPLAY_STATE_PAUSED;
    if (has_preparing_context && time_total <= 0)
        return DENEB_PRINT_DISPLAY_STATE_PREPARING;
    if (is_printing)
        return DENEB_PRINT_DISPLAY_STATE_PRINTING;
    return DENEB_PRINT_DISPLAY_STATE_IDLE;
}

void deneb_print_stop_guard_init(deneb_print_stop_guard_t *guard,
                                 int cooldown_ms)
{
    if (!guard)
        return;
    guard->last_stop_ms = -1;
    guard->in_flight = 0;
    guard->cooldown_ms = cooldown_ms > 0 ? cooldown_ms : 3000;
}

int deneb_print_stop_guard_begin(deneb_print_stop_guard_t *guard,
                                 long long now_ms)
{
    if (!guard)
        return 0;

    if (guard->in_flight && guard->last_stop_ms >= 0 &&
        now_ms - guard->last_stop_ms < guard->cooldown_ms)
        return 0;
    if (guard->in_flight)
        return 0;

    guard->in_flight = 1;
    guard->last_stop_ms = now_ms;
    return 1;
}

int deneb_print_stop_guard_inflight(deneb_print_stop_guard_t *guard,
                                    long long now_ms,
                                    int has_active_context)
{
    if (!guard || !guard->in_flight || guard->last_stop_ms < 0)
        return 0;

    if (now_ms - guard->last_stop_ms < guard->cooldown_ms)
        return 1;
    if (has_active_context)
        return 1;

    deneb_print_stop_guard_clear(guard);
    return 0;
}

void deneb_print_stop_guard_clear(deneb_print_stop_guard_t *guard)
{
    if (!guard)
        return;
    guard->in_flight = 0;
    guard->last_stop_ms = -1;
}

static void normalize_action_value(char *action)
{
    char *start;
    char *end;
    size_t len;

    if (!action)
        return;

    start = action;
    while (*start && ascii_isspace((unsigned char)*start))
        start++;

    end = start + strlen(start);
    while (end > start && ascii_isspace((unsigned char)end[-1]))
        end--;

    if (end <= start) {
        action[0] = '\0';
        return;
    }

    if ((*start == '"' && end[-1] == '"') ||
        (*start == '\'' && end[-1] == '\'')) {
        start++;
        end--;
    }

    len = (size_t)(end - start);
    for (size_t i = 0; i < len; i++)
        action[i] = (char)ascii_tolower((unsigned char)start[i]);
    action[len] = '\0';
}

int deneb_print_action_parse(const char *body, char *out, size_t out_sz)
{
    const char *p;
    size_t i = 0;
    char quote = '\0';

    if (!body || !out || out_sz < 2)
        return -1;
    out[0] = '\0';

    p = strstr(body, "\"action\"");
    if (p) {
        p = strchr(p + 8, ':');
        if (!p)
            return -1;
        p++;
        while (*p && ascii_isspace((unsigned char)*p))
            p++;
        if (*p != '"' && *p != '\'')
            return -1;
        quote = *p++;
        while (*p && *p != quote && i < (size_t)out_sz - 1)
            out[i++] = *p++;
        if (*p != quote)
            return -1;
    } else {
        p = body;
        while (*p && (ascii_isspace((unsigned char)*p) ||
                      *p == '"' || *p == '\''))
            p++;
        while (*p && !ascii_isspace((unsigned char)*p) &&
               *p != '"' && *p != '\'' && *p != '{' && *p != '}' &&
               i < (size_t)out_sz - 1)
            out[i++] = *p++;
    }

    out[i] = '\0';
    normalize_action_value(out);
    return out[0] ? 0 : -1;
}

int deneb_print_action_is_pause(const char *action)
{
    return str_eq_ci(action, DENEB_PRINT_ACTION_PAUSE_TEXT);
}

int deneb_print_action_is_resume_or_start(const char *action)
{
    return str_eq_ci(action, DENEB_PRINT_ACTION_PRINT_TEXT) ||
           str_eq_ci(action, DENEB_PRINT_ACTION_RESUME_TEXT) ||
           str_eq_ci(action, DENEB_PRINT_ACTION_CONTINUE_TEXT) ||
           str_eq_ci(action, DENEB_PRINT_ACTION_FORCE_TEXT) ||
           str_eq_ci(action, DENEB_PRINT_ACTION_START_TEXT);
}

int deneb_print_action_is_abort(const char *action)
{
    return str_eq_ci(action, DENEB_PRINT_ACTION_ABORT_TEXT) ||
           str_eq_ci(action, DENEB_PRINT_ACTION_CANCEL_TEXT);
}

int deneb_print_action_is_stop(const char *action)
{
    return str_eq_ci(action, DENEB_PRINT_ACTION_STOP_TEXT);
}

int deneb_print_action_is_force(const char *action)
{
    return str_eq_ci(action, DENEB_PRINT_ACTION_FORCE_TEXT);
}

void deneb_print_action_plan_init(deneb_print_action_plan_t *plan)
{
    if (!plan)
        return;
    plan->kind = DENEB_PRINT_ACTION_PLAN_NONE;
    plan->command = "";
    plan->failure_message = "Unknown print job action";
    plan->clear_pending_after_success = 0;
}

int deneb_print_action_plan(const char *action,
                            deneb_print_action_plan_t *plan)
{
    if (!plan)
        return -1;

    deneb_print_action_plan_init(plan);
    if (deneb_print_action_is_pause(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_PAUSE;
        plan->command = DENEB_COMMAND_VERB_PAUSE;
        plan->failure_message = "Failed to pause print";
        return 0;
    }

    if (deneb_print_action_is_resume_or_start(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_RESUME;
        plan->command = DENEB_COMMAND_VERB_RESUME;
        plan->failure_message = "Failed to resume print";
        return 0;
    }

    if (deneb_print_action_is_abort(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_ABORT;
        plan->command = DENEB_COMMAND_VERB_ABORT;
        plan->failure_message = "Failed to abort print";
        plan->clear_pending_after_success = 1;
        return 0;
    }

    if (deneb_print_action_is_stop(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_STOP;
        plan->command = DENEB_COMMAND_VERB_ABORT;
        plan->failure_message = "Failed to stop print";
        plan->clear_pending_after_success = 1;
        return 0;
    }

    return -1;
}

int deneb_print_elapsed_seconds(int time_total, int time_left)
{
    if (time_total <= 0)
        return 0;
    if (time_left <= 0)
        return time_total;
    if (time_left >= time_total)
        return 0;
    return time_total - time_left;
}

float deneb_print_progress_percent(int time_total, int time_left)
{
    int elapsed = deneb_print_elapsed_seconds(time_total, time_left);

    if (time_total <= 0 || elapsed <= 0)
        return 0.0f;
    if (elapsed >= time_total)
        return 100.0f;
    return (float)elapsed * 100.0f / (float)time_total;
}

float deneb_print_progress_fraction(float progress_percent)
{
    if (progress_percent <= 0.0f)
        return 0.0f;
    if (progress_percent >= 100.0f)
        return 1.0f;
    return progress_percent / 100.0f;
}

void deneb_print_normalize_timing(int is_printing,
                                  int is_paused,
                                  int *time_total,
                                  int *time_left,
                                  float *progress_percent)
{
    int total = time_total ? *time_total : 0;
    int left = time_left ? *time_left : 0;

    if (total > 0 && left >= 0 && !is_printing && !is_paused) {
        total = 0;
        left = 0;
    }

    if (total > 0 && left > total)
        left = total;

    if (time_total)
        *time_total = total;
    if (time_left)
        *time_left = left;
    if (progress_percent)
        *progress_percent = deneb_print_progress_percent(total, left);
}
