/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_STATUS_H
#define DENEB_PRINTSVC_STATUS_H

#include <stdbool.h>
#include <stddef.h>

#include "error_map.h"

typedef enum {
    DENEB_PRINT_STATE_IDLE = 0,
    DENEB_PRINT_STATE_PREPARING,
    DENEB_PRINT_STATE_PRINTING,
    DENEB_PRINT_STATE_PAUSED,
    DENEB_PRINT_STATE_ABORTING,
    DENEB_PRINT_STATE_COMPLETE,
    DENEB_PRINT_STATE_ERROR
} deneb_print_state_t;

typedef struct {
    deneb_print_state_t state;
    char file[128];
    char source[32];
    char uuid[64];
    char req[32];
    char firmware[64];
    char machine_type[16];
    int pcb_id;
    bool pcb_id_valid;
    float head_t_set;
    float head_t_cur;
    float bed_t_set;
    float bed_t_cur;
    float topcap_t_cur;
    bool topcap_present;
    float x;
    float y;
    float z;
    float e;
    float r0;
    float home_x;
    float home_y;
    float home_z;
    bool home_distance_valid;
    unsigned int flow_inflight;
    unsigned int flow_sent;
    unsigned int flow_ack;
    unsigned int flow_resend;
    unsigned int flow_reject;
    char flow_last_response[96];
    unsigned int job_queue_depth;
    unsigned int job_line_number;
    unsigned int command_latency_ms;
    unsigned int planner_starvation_count;
    unsigned int position_report_count;
    unsigned int finish_drain_ticks;
    unsigned int finish_stable_reports;
    int time_total;
    int time_left;
    bool fault;
    deneb_error_t error;
} deneb_status_t;

void deneb_status_init(deneb_status_t *status);
const char *deneb_status_state_name(deneb_print_state_t state);
int deneb_status_has_active_print(const deneb_status_t *status);
int deneb_status_serialize_payload(const deneb_status_t *status, char *out, size_t out_sz);
int deneb_status_serialize_frame(const deneb_status_t *status, char *out, size_t out_sz);

#endif
