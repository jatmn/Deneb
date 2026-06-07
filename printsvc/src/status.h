/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_STATUS_H
#define DENEB_PRINTSVC_STATUS_H

#include <stdbool.h>
#include <stddef.h>

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
    float head_t_set;
    float head_t_cur;
    float bed_t_set;
    float bed_t_cur;
    float x;
    float y;
    float z;
    float e;
    int time_total;
    int time_left;
    bool fault;
} deneb_status_t;

void deneb_status_init(deneb_status_t *status);
const char *deneb_status_state_name(deneb_print_state_t state);
int deneb_status_has_active_print(const deneb_status_t *status);
int deneb_status_serialize_payload(const deneb_status_t *status, char *out, size_t out_sz);
int deneb_status_serialize_frame(const deneb_status_t *status, char *out, size_t out_sz);

#endif
