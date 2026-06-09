/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_STATUS_PAYLOAD_H
#define DENEB_STATUS_PAYLOAD_H

#include "print_state_rules.h"

#include <stddef.h>

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
    char firmware[64];
    char machine_type[16];
    int pcb_id;
    int pcb_id_valid;
    int has_file;
    int is_printing;
    int is_paused;
    int has_error;
    int native_active;
    int native_stop_allowed;
    int has_native_active;
    int has_native_stop_allowed;
    deneb_print_observation_t observation;
} deneb_status_payload_t;

typedef struct {
    const char *req;
    const char *filename;
    const char *uuid;
    int time_total;
    int time_left;
    float bed_target;
    float nozzle_target;
    int is_printing;
    int is_paused;
} deneb_status_filename_context_t;

void deneb_status_payload_init(deneb_status_payload_t *payload);
int deneb_status_payload_parse(const char *json,
                               deneb_status_payload_t *payload);
void deneb_status_filename_context_init(
    deneb_status_filename_context_t *ctx,
    const char *req,
    const char *filename,
    const char *uuid,
    int time_total,
    int time_left,
    float bed_target,
    float nozzle_target,
    int is_printing,
    int is_paused);
deneb_status_filename_context_t deneb_status_filename_context_from_fields(
    const char *req,
    const char *filename,
    const char *uuid,
    int time_total,
    int time_left,
    float bed_target,
    float nozzle_target,
    int is_printing,
    int is_paused);
int deneb_status_payload_should_hold_filename(
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev);
void deneb_status_payload_resolve_filename(
    const deneb_status_payload_t *payload,
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev,
    char *retained,
    size_t retained_sz,
    char *out,
    size_t out_sz);
void deneb_status_payload_resolve_filename_value(
    const char *file,
    int has_file,
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev,
    char *retained,
    size_t retained_sz,
    char *out,
    size_t out_sz);

#endif
