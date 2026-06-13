/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_STATUS_STATE_H
#define DENEB_STATUS_STATE_H

#include "print_state_rules.h"
#include "status_payload.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float nozzle_temp_cur;
    float nozzle_temp_set;
    float bed_temp_cur;
    float bed_temp_set;
    float topcap_temp_cur;
    bool topcap_present;

    float pos_x;
    float pos_y;
    float pos_z;
    float pos_e;

    char filename[128];
    char source[32];
    char uuid[64];
    char cloud_job_id[96];
    char firmware[64];
    char machine_type[16];
    char error_key[32];
    char error_category[32];
    char error_detail[128];
    char flow_last_response[128];
    int pcb_id;
    bool pcb_id_valid;
    int flow_inflight;
    int flow_sent;
    int flow_ack;
    int flow_resend;
    int flow_reject;
    int job_line_number;
    int time_total;
    int time_left;
    float progress;

    bool is_printing;
    bool is_paused;
    bool has_error;
    bool native_active;
    bool native_stop_allowed;
    bool has_native_active;
    bool has_native_stop_allowed;
    char current_req[32];

    bool connected;
    uint32_t last_update_ms;
} deneb_backend_status_state_t;

typedef struct {
    int req_changed;
    int print_resumed;
    int print_paused;
    int print_ended;
    int print_started;
    const char *completion_label;
} deneb_status_transition_t;

void deneb_status_state_init(deneb_backend_status_state_t *state);
void deneb_status_transition_init(deneb_status_transition_t *transition);
deneb_status_filename_context_t deneb_status_state_filename_context(
    const deneb_backend_status_state_t *state);
int deneb_status_state_has_print_name(
    const deneb_backend_status_state_t *state,
    const char *display_name);
int deneb_status_state_has_print_context(
    const deneb_backend_status_state_t *state);
int deneb_status_state_has_abort_context(
    const deneb_backend_status_state_t *state,
    int stop_inflight);
deneb_print_context_flags_t deneb_status_state_context_flags(
    const deneb_backend_status_state_t *state,
    int has_print_name);
int deneb_status_state_transition_from_pair(
    deneb_status_transition_t *transition,
    const deneb_backend_status_state_t *prev,
    const deneb_backend_status_state_t *curr);
int deneb_status_state_preheat_events(
    const deneb_backend_status_state_t *state,
    deneb_print_preheat_tracker_t *tracker);
int deneb_status_state_apply_json(deneb_backend_status_state_t *state,
                                  const deneb_backend_status_state_t *prev,
                                  const char *json,
                                  char *retained_filename,
                                  size_t retained_filename_sz,
                                  deneb_print_stop_guard_t *stop_guard,
                                  uint32_t now_ms);

#endif
