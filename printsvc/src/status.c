/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "status.h"

#include <stdio.h>
#include <string.h>

void deneb_status_init(deneb_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = DENEB_PRINT_STATE_IDLE;
    strcpy(status->file, "none");
}

const char *deneb_status_state_name(deneb_print_state_t state)
{
    switch (state) {
        case DENEB_PRINT_STATE_PREPARING: return "Preparing";
        case DENEB_PRINT_STATE_PRINTING: return "Printing";
        case DENEB_PRINT_STATE_PAUSED: return "Paused";
        case DENEB_PRINT_STATE_ABORTING: return "Aborting";
        case DENEB_PRINT_STATE_COMPLETE: return "Complete";
        case DENEB_PRINT_STATE_ERROR: return "Error";
        case DENEB_PRINT_STATE_IDLE:
        default: return "Idle";
    }
}

int deneb_status_has_active_print(const deneb_status_t *status)
{
    if (!status)
        return 0;

    return status->state == DENEB_PRINT_STATE_PREPARING ||
           status->state == DENEB_PRINT_STATE_PRINTING ||
           status->state == DENEB_PRINT_STATE_PAUSED ||
           status->state == DENEB_PRINT_STATE_ABORTING;
}

static const char *safe_file(const char *file)
{
    return file && file[0] ? file : "none";
}

int deneb_status_serialize_payload(const deneb_status_t *status, char *out, size_t out_sz)
{
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "{\"file\":\"%s\",\"name\":\"%s\",\"headTset\":%.1f,"
                 "\"headTcur\":%.1f,\"bedTset\":%.1f,\"bedTcur\":%.1f,"
                 "\"X\":%.3f,\"Y\":%.3f,\"Z\":%.3f,\"E\":%.3f,"
                 "\"uuid\":\"%s\",\"source\":\"%s\",\"Ttot\":%d,"
                 "\"Tleft\":%d,\"req\":\"%s\",\"received_faults\":%s}",
                 safe_file(status->file), safe_file(status->file),
                 status->head_t_set, status->head_t_cur,
                 status->bed_t_set, status->bed_t_cur,
                 status->x, status->y, status->z, status->e,
                 status->uuid, status->source,
                 status->time_total, status->time_left,
                 status->req[0] ? status->req : deneb_status_state_name(status->state),
                 status->fault ? "[1]" : "[]");

    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_status_serialize_frame(const deneb_status_t *status, char *out, size_t out_sz)
{
    char payload[768];
    int payload_len;
    int n;

    payload_len = deneb_status_serialize_payload(status, payload, sizeof(payload));
    if (payload_len < 0)
        return -1;

    n = snprintf(out, out_sz, "%s<%s", DENEB_PRINTSVC_STATUS_TOPIC, payload);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
