/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "json_string.h"
#include "print_control.h"
#include "status.h"

#include <stdio.h>
#include <string.h>

void deneb_status_init(deneb_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = DENEB_PRINT_STATE_IDLE;
    strcpy(status->file, "none");
    deneb_error_clear(&status->error);
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

    return deneb_print_control_phase_active(
        deneb_print_control_phase_from_state(status->state));
}

static const char *safe_file(const char *file)
{
    return file && file[0] ? file : "none";
}

int deneb_status_serialize_payload(const deneb_status_t *status, char *out, size_t out_sz)
{
    char file[256];
    char uuid[128];
    char source[80];
    char req[80];
    char error_detail[256];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(safe_file(status->file), file, sizeof(file));
    deneb_json_escape_string(status->uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(status->source, source, sizeof(source));
    deneb_json_escape_string(status->req[0] ? status->req : deneb_status_state_name(status->state),
                             req, sizeof(req));
    deneb_json_escape_string(status->error.detail, error_detail, sizeof(error_detail));

    n = snprintf(out, out_sz,
                 "{\"file\":\"%s\",\"name\":\"%s\",\"headTset\":%.1f,"
                 "\"headTcur\":%.1f,\"bedTset\":%.1f,\"bedTcur\":%.1f,"
                 "\"X\":%.3f,\"Y\":%.3f,\"Z\":%.3f,\"E\":%.3f,"
                 "\"homeDistanceX\":%.3f,\"homeDistanceY\":%.3f,"
                 "\"homeDistanceZ\":%.3f,\"homeDistanceValid\":%s,"
                 "\"flowInflight\":%u,\"flowSent\":%u,\"flowAck\":%u,"
                 "\"flowResend\":%u,\"flowReject\":%u,"
                 "\"jobQueueDepth\":%u,\"jobLineNumber\":%u,"
                 "\"commandLatencyMs\":%u,\"plannerStarvationCount\":%u,"
                 "\"denebState\":\"%s\",\"denebActive\":%s,"
                 "\"denebStopAllowed\":%s,"
                 "\"denebErrorKey\":\"%s\",\"denebErrorCategory\":\"%s\","
                 "\"denebErrorDetail\":\"%s\","
                 "\"uuid\":\"%s\",\"source\":\"%s\",\"Ttot\":%d,"
                 "\"Tleft\":%d,\"req\":\"%s\",\"received_faults\":%s}",
                 file, file,
                 status->head_t_set, status->head_t_cur,
                 status->bed_t_set, status->bed_t_cur,
                 status->x, status->y, status->z, status->e,
                 status->home_x, status->home_y, status->home_z,
                 status->home_distance_valid ? "true" : "false",
                 status->flow_inflight, status->flow_sent, status->flow_ack,
                 status->flow_resend, status->flow_reject,
                 status->job_queue_depth, status->job_line_number,
                 status->command_latency_ms, status->planner_starvation_count,
                 deneb_print_control_phase_name(
                     deneb_print_control_phase_from_state(status->state)),
                 deneb_status_has_active_print(status) ? "true" : "false",
                 deneb_print_control_phase_stop_allowed(
                     deneb_print_control_phase_from_state(status->state)) ? "true" : "false",
                 status->error.key ? status->error.key : "none",
                 status->error.category ? status->error.category : "none",
                 error_detail,
                 uuid, source,
                 status->time_total, status->time_left,
                 req, status->fault ? "[1]" : "[]");

    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}

int deneb_status_serialize_frame(const deneb_status_t *status, char *out, size_t out_sz)
{
    char payload[1536];
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
