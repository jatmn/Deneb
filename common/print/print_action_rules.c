/* SPDX-License-Identifier: MPL-2.0 */
#include "print_state_rules.h"
#include "command_format.h"
#include "print_string.h"

#include <stdio.h>
#include <string.h>

static void normalize_action_value(char *action)
{
    char *start;
    char *end;
    size_t len;

    if (!action)
        return;

    start = action;
    while (*start && deneb_ascii_isspace((unsigned char)*start))
        start++;

    end = start + strlen(start);
    while (end > start && deneb_ascii_isspace((unsigned char)end[-1]))
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
        action[i] = (char)deneb_ascii_tolower((unsigned char)start[i]);
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
        while (*p && deneb_ascii_isspace((unsigned char)*p))
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
        while (*p && (deneb_ascii_isspace((unsigned char)*p) ||
                      *p == '"' || *p == '\''))
            p++;
        while (*p && !deneb_ascii_isspace((unsigned char)*p) &&
               *p != '"' && *p != '\'' && *p != '{' && *p != '}' &&
               i < (size_t)out_sz - 1)
            out[i++] = *p++;
    }

    out[i] = '\0';
    normalize_action_value(out);
    return out[0] ? 0 : -1;
}

int deneb_print_action_parse_or_pending_default(const char *body,
                                                int has_pending_job,
                                                char *out,
                                                size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;

    if (deneb_print_action_parse(body, out, out_sz) == 0)
        return 0;

    if (!has_pending_job || out_sz <= strlen(DENEB_PRINT_ACTION_PRINT_TEXT))
        return -1;

    snprintf(out, out_sz, "%s", DENEB_PRINT_ACTION_PRINT_TEXT);
    return 0;
}

const char *deneb_print_action_parse_error_response(void)
{
    return "{\"message\":\"Expected {\\\"action\\\":\\\"pause|print|abort\\\"}\"}";
}

const char *deneb_print_action_unknown_response(void)
{
    return "{\"message\":\"Unknown print job action\"}";
}

const char *deneb_print_state_unknown_response(void)
{
    return "{\"message\":\"Unknown state\"}";
}

int deneb_print_action_is_pause(const char *action)
{
    return deneb_str_eq_ci(action, DENEB_PRINT_ACTION_PAUSE_TEXT);
}

int deneb_print_action_is_resume_or_start(const char *action)
{
    return deneb_str_eq_ci(action, DENEB_PRINT_ACTION_PRINT_TEXT) ||
           deneb_str_eq_ci(action, DENEB_PRINT_ACTION_RESUME_TEXT) ||
           deneb_str_eq_ci(action, DENEB_PRINT_ACTION_CONTINUE_TEXT) ||
           deneb_str_eq_ci(action, DENEB_PRINT_ACTION_FORCE_TEXT) ||
           deneb_str_eq_ci(action, DENEB_PRINT_ACTION_START_TEXT);
}

int deneb_print_action_is_abort(const char *action)
{
    return deneb_str_eq_ci(action, DENEB_PRINT_ACTION_ABORT_TEXT) ||
           deneb_str_eq_ci(action, DENEB_PRINT_ACTION_CANCEL_TEXT);
}

int deneb_print_action_is_stop(const char *action)
{
    return deneb_str_eq_ci(action, DENEB_PRINT_ACTION_STOP_TEXT);
}

int deneb_print_action_is_force(const char *action)
{
    return deneb_str_eq_ci(action, DENEB_PRINT_ACTION_FORCE_TEXT);
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

int deneb_print_pending_action_plan(const char *action,
                                    deneb_print_action_plan_t *plan)
{
    if (!plan)
        return -1;

    deneb_print_action_plan_init(plan);
    if (deneb_print_action_is_resume_or_start(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_RESUME;
        plan->command = DENEB_PRINT_REQ_PREPARE;
        plan->failure_message = "Failed to continue print";
        return 0;
    }

    if (deneb_print_action_is_abort(action)) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_ABORT;
        plan->command = DENEB_COMMAND_VERB_ABORT;
        plan->failure_message = "Failed to cancel print";
        plan->clear_pending_after_success = 1;
        return 0;
    }

    return -1;
}

int deneb_print_delete_action_plan(int has_active_job,
                                   deneb_print_action_plan_t *plan)
{
    if (!plan)
        return -1;

    deneb_print_action_plan_init(plan);
    if (has_active_job) {
        plan->kind = DENEB_PRINT_ACTION_PLAN_ABORT;
        plan->command = DENEB_COMMAND_VERB_ABORT;
        plan->failure_message = "Failed to abort print";
        return 0;
    }

    plan->kind = DENEB_PRINT_ACTION_PLAN_CLEAR_PENDING;
    plan->failure_message = "Failed to clear pending print";
    plan->clear_pending_after_success = 1;
    return 0;
}

int deneb_print_cluster_action_plan(const char *action,
                                    int has_pending_job,
                                    deneb_print_action_plan_t *plan,
                                    deneb_print_action_route_t *route)
{
    if (!plan || !route)
        return -1;

    deneb_print_action_plan_init(plan);
    *route = DENEB_PRINT_ACTION_ROUTE_NORMAL;

    if (has_pending_job &&
        deneb_print_pending_action_plan(action, plan) == 0) {
        *route = DENEB_PRINT_ACTION_ROUTE_PENDING;
        return 0;
    }

    if (deneb_print_action_plan(action, plan) == 0)
        return 0;

    return -1;
}
