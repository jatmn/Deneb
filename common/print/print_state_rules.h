/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_STATE_RULES_H
#define DENEB_PRINT_STATE_RULES_H

typedef struct {
    const char *req;
    const char *file;
    int time_total;
    int time_left;
    float bed_target;
    float nozzle_target;
} deneb_print_observation_t;

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
int deneb_print_active_time(int time_total, int time_left);
int deneb_print_observation_has_context(const deneb_print_observation_t *obs);
int deneb_print_has_active_context(const deneb_print_observation_t *obs,
                                   int is_printing, int is_paused,
                                   int has_print_name);
int deneb_print_has_preparing_context(const deneb_print_observation_t *obs,
                                      int has_print_name);
int deneb_print_has_stoppable_context(const deneb_print_observation_t *obs,
                                      int is_printing, int is_paused,
                                      int has_print_name);
const char *deneb_print_status_label(int connected, int has_error,
                                     int is_paused, int is_active);
const char *deneb_print_job_status_label(int has_error, int is_paused,
                                         int is_active);
const char *deneb_print_job_state_or_none(int has_error, int is_paused,
                                          int is_active);
const char *deneb_print_completion_state_label(int has_error, int time_total,
                                               int time_left);
int deneb_print_job_is_active(int has_error, int is_paused, int is_active);
int deneb_print_manual_action_allowed(int connected, int has_error,
                                      int is_paused, int is_active);
int deneb_print_elapsed_seconds(int time_total, int time_left);
float deneb_print_progress_percent(int time_total, int time_left);
float deneb_print_progress_fraction(float progress_percent);

#endif
