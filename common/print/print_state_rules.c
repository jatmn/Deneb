/* SPDX-License-Identifier: MPL-2.0 */
#include "print_state_rules.h"
#include "command_format.h"
#include "print_macros.h"
#include "print_string.h"

#include <string.h>

int deneb_print_req_is_print(const char *req)
{
    static const char *const print_reqs[] = {
        DENEB_COMMAND_VERB_JOB, "Print", DENEB_PRINT_REQ_PRINTING,
        DENEB_COMMAND_VERB_PAUSE, "Pause", DENEB_PRINT_REQ_PAUSED,
        NULL
    };

    return deneb_str_is_one_of_ci(req, print_reqs);
}

int deneb_print_req_is_paused(const char *req)
{
    static const char *const paused_reqs[] = {
        DENEB_COMMAND_VERB_PAUSE, "Pause", DENEB_PRINT_REQ_PAUSED,
        NULL
    };

    return deneb_str_is_one_of_ci(req, paused_reqs);
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

    return deneb_str_is_one_of_ci(req, lifecycle_reqs);
}

int deneb_print_req_is_abort(const char *req)
{
    static const char *const abort_reqs[] = {
        DENEB_COMMAND_VERB_ABORT, "Abort", DENEB_PRINT_REQ_ABORTING, "ABORTING",
        "BUSY_ABORTING",
        NULL
    };

    return deneb_str_is_one_of_ci(req, abort_reqs);
}

int deneb_print_file_is_transient(const char *file)
{
    if (!file || !*file || strcmp(file, DENEB_PRINT_NONE_VALUE) == 0)
        return 0;

    if (deneb_str_contains_ci(file, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD))
        return 1;
    if (deneb_str_contains_ci(file, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_UP))
        return 1;
    if (deneb_str_contains_ci(file, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_DOWN))
        return 1;
    if (deneb_str_contains_ci(file, "macro") &&
        deneb_str_contains_ci(file, ".gcode"))
        return 1;

    return 0;
}

int deneb_print_file_is_candidate(const char *file)
{
    if (!file || !*file || strcmp(file, DENEB_PRINT_NONE_VALUE) == 0)
        return 0;

    return (deneb_str_contains_ci(file, ".gcode") ||
            deneb_str_contains_ci(file, ".ufp")) &&
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

void deneb_print_context_flags_init(deneb_print_context_flags_t *flags)
{
    if (!flags)
        return;

    flags->has_active_context = 0;
    flags->has_preparing_context = 0;
    flags->has_stoppable_context = 0;
}

void deneb_print_context_flags_from_observation(
    deneb_print_context_flags_t *flags,
    const deneb_print_observation_t *obs,
    int is_printing,
    int is_paused,
    int has_print_name)
{
    if (!flags)
        return;

    deneb_print_context_flags_init(flags);
    if (!obs)
        return;

    flags->has_active_context =
        deneb_print_has_active_context(obs, is_printing, is_paused,
                                       has_print_name);
    flags->has_preparing_context =
        deneb_print_has_preparing_context(obs, has_print_name);
    flags->has_stoppable_context =
        deneb_print_has_stoppable_context(obs, is_printing, is_paused,
                                          has_print_name);
}

void deneb_print_context_flags_from_fields(
    deneb_print_context_flags_t *flags,
    const char *req,
    const char *file,
    int time_total,
    int time_left,
    float bed_target,
    float nozzle_target,
    int is_printing,
    int is_paused,
    int has_print_name)
{
    deneb_print_observation_t obs;

    deneb_print_observation_init(&obs, req, file, time_total, time_left,
                                 bed_target, nozzle_target);
    deneb_print_context_flags_from_observation(
        flags, &obs, is_printing, is_paused, has_print_name);
}

int deneb_print_fields_have_active_context(const char *req,
                                           const char *file,
                                           int time_total,
                                           int time_left,
                                           float bed_target,
                                           float nozzle_target,
                                           int is_printing,
                                           int is_paused,
                                           int has_print_name)
{
    deneb_print_context_flags_t flags;

    deneb_print_context_flags_from_fields(
        &flags, req, file, time_total, time_left, bed_target, nozzle_target,
        is_printing, is_paused, has_print_name);
    return flags.has_active_context;
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

void deneb_print_preheat_tracker_init(deneb_print_preheat_tracker_t *tracker)
{
    if (!tracker)
        return;
    tracker->targets_seen = 0;
    tracker->targets_ready_seen = 0;
}

int deneb_print_preheat_tracker_update(deneb_print_preheat_tracker_t *tracker,
                                       float bed_current,
                                       float bed_target,
                                       float nozzle_current,
                                       float nozzle_target)
{
    int events = DENEB_PRINT_PREHEAT_EVENT_NONE;
    int has_targets = deneb_print_has_temp_targets(bed_target, nozzle_target);

    if (!tracker)
        return events;

    if (!has_targets) {
        if (tracker->targets_seen || tracker->targets_ready_seen)
            events |= DENEB_PRINT_PREHEAT_EVENT_RESET;
        deneb_print_preheat_tracker_init(tracker);
        return events;
    }

    if (!tracker->targets_seen) {
        tracker->targets_seen = 1;
        events |= DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE;
    }

    if (!tracker->targets_ready_seen &&
        deneb_print_temp_targets_ready(bed_current, bed_target,
                                       nozzle_current, nozzle_target)) {
        tracker->targets_ready_seen = 1;
        events |= DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY;
    }

    return events;
}
