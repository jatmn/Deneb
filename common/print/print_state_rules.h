/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_STATE_RULES_H
#define DENEB_PRINT_STATE_RULES_H

#include <stddef.h>

#define DENEB_PRINT_DEFAULT_JOB_UUID "deneb-current-job"
#define DENEB_PRINT_DEFAULT_JOB_NAME "Current print"
#define DENEB_PRINT_DEFAULT_JOB_SOURCE "Cura"
#define DENEB_PRINT_NONE_VALUE "none"
#define DENEB_PRINT_USB_JOB_SOURCE "USB"
#define DENEB_PRINT_WEB_API_JOB_SOURCE "WEB_API"
#define DENEB_PRINT_STOCK_API_JOB_UUID "0"
#define DENEB_PRINT_PHASE_NAME_PRE_PRINT "pre_print"
#define DENEB_PRINT_REQ_IDLE "Idle"
#define DENEB_PRINT_REQ_PREPARE "PREPARE"
#define DENEB_PRINT_REQ_PREHEAT "PREHEAT"
#define DENEB_PRINT_REQ_PREHEATING "PREHEATING"
#define DENEB_PRINT_REQ_PRINTING "Printing"
#define DENEB_PRINT_REQ_PAUSED "Paused"
#define DENEB_PRINT_REQ_ABORTING "Aborting"
#define DENEB_PRINT_REQ_COMPLETE "Complete"
#define DENEB_PRINT_REQ_ERROR "Error"
#define DENEB_PRINT_ACTION_PAUSE_TEXT "pause"
#define DENEB_PRINT_ACTION_PRINT_TEXT "print"
#define DENEB_PRINT_ACTION_RESUME_TEXT "resume"
#define DENEB_PRINT_ACTION_CONTINUE_TEXT "continue"
#define DENEB_PRINT_ACTION_FORCE_TEXT "force"
#define DENEB_PRINT_ACTION_START_TEXT "start"
#define DENEB_PRINT_ACTION_ABORT_TEXT "abort"
#define DENEB_PRINT_ACTION_CANCEL_TEXT "cancel"
#define DENEB_PRINT_ACTION_STOP_TEXT "stop"
#define DENEB_PRINT_MATERIAL_MIN_MOVE_TEMP_C 170.0f
#define DENEB_PRINT_MATERIAL_READY_TOLERANCE_C 2.0f

typedef struct {
    const char *req;
    const char *file;
    int time_total;
    int time_left;
    float bed_target;
    float nozzle_target;
} deneb_print_observation_t;

typedef struct {
    long long last_stop_ms;
    int in_flight;
    int cooldown_ms;
} deneb_print_stop_guard_t;

typedef struct {
    int targets_seen;
    int targets_ready_seen;
} deneb_print_preheat_tracker_t;

typedef struct {
    int has_active_context;
    int has_preparing_context;
    int has_stoppable_context;
} deneb_print_context_flags_t;

typedef enum {
    DENEB_PRINT_PREHEAT_EVENT_NONE = 0,
    DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE = 1,
    DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY = 2,
    DENEB_PRINT_PREHEAT_EVENT_RESET = 4
} deneb_print_preheat_event_t;

typedef enum {
    DENEB_PRINT_ACTION_PLAN_NONE = 0,
    DENEB_PRINT_ACTION_PLAN_PAUSE,
    DENEB_PRINT_ACTION_PLAN_RESUME,
    DENEB_PRINT_ACTION_PLAN_ABORT,
    DENEB_PRINT_ACTION_PLAN_STOP,
    DENEB_PRINT_ACTION_PLAN_CLEAR_PENDING
} deneb_print_action_plan_kind_t;

typedef enum {
    DENEB_PRINT_DISPLAY_STATE_IDLE = 0,
    DENEB_PRINT_DISPLAY_STATE_PRINTING,
    DENEB_PRINT_DISPLAY_STATE_PAUSED,
    DENEB_PRINT_DISPLAY_STATE_PREPARING,
    DENEB_PRINT_DISPLAY_STATE_COOLING,
    DENEB_PRINT_DISPLAY_STATE_ERROR
} deneb_print_display_state_t;

typedef struct {
    deneb_print_action_plan_kind_t kind;
    const char *command;
    const char *failure_message;
    int clear_pending_after_success;
} deneb_print_action_plan_t;

int deneb_print_req_is_print(const char *req);
int deneb_print_req_is_paused(const char *req);
int deneb_print_req_is_lifecycle(const char *req);
int deneb_print_req_is_abort(const char *req);
int deneb_print_file_is_transient(const char *file);
int deneb_print_file_is_candidate(const char *file);
int deneb_print_has_temp_targets(float bed_target, float nozzle_target);
int deneb_print_temp_target_ready(float current, float target, float tolerance);
int deneb_print_temp_targets_ready(float bed_current, float bed_target,
                                   float nozzle_current, float nozzle_target);
int deneb_print_material_move_ready(float current_nozzle_temp,
                                    float target_nozzle_temp);
int deneb_print_active_time(int time_total, int time_left);
void deneb_print_observation_init(deneb_print_observation_t *obs,
                                  const char *req,
                                  const char *file,
                                  int time_total,
                                  int time_left,
                                  float bed_target,
                                  float nozzle_target);
int deneb_print_observation_has_context(const deneb_print_observation_t *obs);
int deneb_print_has_active_context(const deneb_print_observation_t *obs,
                                   int is_printing, int is_paused,
                                   int has_print_name);
int deneb_print_has_preparing_context(const deneb_print_observation_t *obs,
                                      int has_print_name);
int deneb_print_has_stoppable_context(const deneb_print_observation_t *obs,
                                      int is_printing, int is_paused,
                                      int has_print_name);
void deneb_print_context_flags_init(deneb_print_context_flags_t *flags);
void deneb_print_context_flags_from_observation(
    deneb_print_context_flags_t *flags,
    const deneb_print_observation_t *obs,
    int is_printing,
    int is_paused,
    int has_print_name);
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
    int has_print_name);
int deneb_print_fields_have_active_context(const char *req,
                                           const char *file,
                                           int time_total,
                                           int time_left,
                                           float bed_target,
                                           float nozzle_target,
                                           int is_printing,
                                           int is_paused,
                                           int has_print_name);
const char *deneb_print_status_label(int connected, int has_error,
                                     int is_paused, int is_active);
const char *deneb_print_job_status_label(int has_error, int is_paused,
                                         int is_active);
const char *deneb_print_job_state_or_none(int has_error, int is_paused,
                                          int is_active);
const char *deneb_print_job_name_or_default(const char *name);
const char *deneb_print_job_uuid_or_default(const char *uuid);
const char *deneb_print_job_source_or_default(const char *source);
const char *deneb_print_completion_state_label(int has_error, int time_total,
                                               int time_left);
int deneb_print_job_is_active(int has_error, int is_paused, int is_active);
int deneb_print_start_allowed(int connected, int has_error,
                              int is_paused, int is_active);
int deneb_print_manual_action_allowed(int connected, int has_error,
                                      int is_paused, int is_active);
deneb_print_display_state_t deneb_print_display_state(
    int connected,
    int has_error,
    int is_paused,
    int is_printing,
    int has_abort_context,
    int has_preparing_context,
    int time_total);
void deneb_print_stop_guard_init(deneb_print_stop_guard_t *guard,
                                 int cooldown_ms);
int deneb_print_stop_guard_begin(deneb_print_stop_guard_t *guard,
                                 long long now_ms);
int deneb_print_stop_guard_inflight(deneb_print_stop_guard_t *guard,
                                    long long now_ms,
                                    int has_active_context);
void deneb_print_stop_guard_clear(deneb_print_stop_guard_t *guard);
void deneb_print_preheat_tracker_init(deneb_print_preheat_tracker_t *tracker);
int deneb_print_preheat_tracker_update(deneb_print_preheat_tracker_t *tracker,
                                       float bed_current,
                                       float bed_target,
                                       float nozzle_current,
                                       float nozzle_target);
int deneb_print_action_parse(const char *body, char *out, size_t out_sz);
int deneb_print_action_parse_or_pending_default(const char *body,
                                                int has_pending_job,
                                                char *out,
                                                size_t out_sz);
const char *deneb_print_action_parse_error_response(void);
const char *deneb_print_action_unknown_response(void);
const char *deneb_print_state_unknown_response(void);
int deneb_print_action_is_pause(const char *action);
int deneb_print_action_is_resume_or_start(const char *action);
int deneb_print_action_is_abort(const char *action);
int deneb_print_action_is_stop(const char *action);
int deneb_print_action_is_force(const char *action);
void deneb_print_action_plan_init(deneb_print_action_plan_t *plan);
int deneb_print_action_plan(const char *action,
                            deneb_print_action_plan_t *plan);
int deneb_print_pending_action_plan(const char *action,
                                    deneb_print_action_plan_t *plan);
int deneb_print_delete_action_plan(int has_active_job,
                                   deneb_print_action_plan_t *plan);
int deneb_print_elapsed_seconds(int time_total, int time_left);
float deneb_print_progress_percent(int time_total, int time_left);
float deneb_print_progress_fraction(float progress_percent);
void deneb_print_normalize_timing(int is_printing,
                                  int is_paused,
                                  int *time_total,
                                  int *time_left,
                                  float *progress_percent);

#endif
