/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_STATUS_PAYLOAD_H
#define DENEB_STATUS_PAYLOAD_H

#include "print_state_rules.h"

typedef struct {
    float nozzle_temp_cur;
    float nozzle_temp_set;
    float bed_temp_cur;
    float bed_temp_set;
    float topcap_temp_cur;
    int topcap_present;
    float pos_x;
    float pos_y;
    float pos_z;
    float pos_e;
    int time_total;
    int time_left;
    float progress;
    char file[256];
    char source[32];
    char uuid[64];
    char req[32];
    int has_file;
    int is_printing;
    int is_paused;
    int has_error;
    deneb_print_observation_t observation;
} deneb_status_payload_t;

void deneb_status_payload_init(deneb_status_payload_t *payload);
int deneb_status_payload_parse(const char *json,
                               deneb_status_payload_t *payload);

#endif
