/* SPDX-License-Identifier: MPL-2.0 */
#include "print_state_rules.h"
#include "print_macros.h"

#include <stddef.h>
#include <string.h>

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
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
        "JOB", "Print", "Printing",
        "PAUSE", "Pause", "Paused",
        NULL
    };

    return str_is_one_of_ci(req, print_reqs);
}

int deneb_print_req_is_paused(const char *req)
{
    static const char *const paused_reqs[] = {
        "PAUSE", "Pause", "Paused",
        NULL
    };

    return str_is_one_of_ci(req, paused_reqs);
}

int deneb_print_req_is_lifecycle(const char *req)
{
    static const char *const lifecycle_reqs[] = {
        "HOME", "HOMING", "HOME_AND_CENTER_HEAD",
        "RESOLVE_CONFLICTS", "PREPARE", "PREHEAT", "PREHEATING",
        "BED_PREHEATING", "HEAT_BED", "BED_AND_NOZZLE_PREHEATING",
        "EXTRACT", "EXTRACTING",
        NULL
    };

    return str_is_one_of_ci(req, lifecycle_reqs);
}

int deneb_print_req_is_abort(const char *req)
{
    static const char *const abort_reqs[] = {
        "ABORT", "Abort", "Aborting", "ABORTING", "BUSY_ABORTING",
        NULL
    };

    return str_is_one_of_ci(req, abort_reqs);
}

int deneb_print_file_is_transient(const char *file)
{
    if (!file || !*file || strcmp(file, "none") == 0)
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
    if (!file || !*file || strcmp(file, "none") == 0)
        return 0;

    return (str_contains_ci(file, ".gcode") || str_contains_ci(file, ".ufp")) &&
           !deneb_print_file_is_transient(file);
}

int deneb_print_has_temp_targets(float bed_target, float nozzle_target)
{
    return bed_target > 0.0f || nozzle_target > 0.0f;
}

int deneb_print_temp_targets_ready(float bed_current, float bed_target,
                                   float nozzle_current, float nozzle_target)
{
    return (bed_target > 0.0f && bed_current >= bed_target - 1.0f) &&
           (nozzle_target <= 0.0f || nozzle_current >= nozzle_target - 1.0f);
}

int deneb_print_active_time(int time_total, int time_left)
{
    return time_total > 0 && time_left > 0 && time_left <= time_total;
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

int deneb_print_job_is_active(int has_error, int is_paused, int is_active)
{
    return has_error || is_paused || is_active;
}
