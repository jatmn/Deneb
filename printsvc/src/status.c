/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "json_string.h"
#include "print_control.h"
#include "print_state_rules.h"
#include "status.h"

#include <stdio.h>
#include <string.h>

void deneb_status_init(deneb_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->state = DENEB_PRINT_STATE_IDLE;
    strcpy(status->file, DENEB_PRINT_NONE_VALUE);
    deneb_error_clear(&status->error);
}

const char *deneb_status_state_name(deneb_print_state_t state)
{
    switch (state) {
        case DENEB_PRINT_STATE_PREPARING: return DENEB_PRINT_REQ_PREPARE;
        case DENEB_PRINT_STATE_PRINTING: return DENEB_PRINT_REQ_PRINTING;
        case DENEB_PRINT_STATE_PAUSED: return DENEB_PRINT_REQ_PAUSED;
        case DENEB_PRINT_STATE_ABORTING: return DENEB_PRINT_REQ_ABORTING;
        case DENEB_PRINT_STATE_COMPLETE: return DENEB_PRINT_REQ_COMPLETE;
        case DENEB_PRINT_STATE_ERROR: return DENEB_PRINT_REQ_ERROR;
        case DENEB_PRINT_STATE_IDLE:
        default: return DENEB_PRINT_REQ_IDLE;
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
    return file && file[0] ? file : DENEB_PRINT_NONE_VALUE;
}

static const char *safe_value(const char *value)
{
    return value && value[0] ? value : DENEB_PRINT_NONE_VALUE;
}

int deneb_status_serialize_payload(const deneb_status_t *status, char *out, size_t out_sz)
{
    char file[256];
    char uuid[128];
    char source[80];
    char cloud_job_id[192];
    char req[80];
    char firmware[128];
    char machine_type[48];
    char error_detail[256];
    char flow_last_response[192];
    int n;

    if (!status || !out || out_sz == 0)
        return -1;

    deneb_json_escape_string(safe_file(status->file), file, sizeof(file));
    deneb_json_escape_string(status->uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(status->source, source, sizeof(source));
    deneb_json_escape_string(status->cloud_job_id, cloud_job_id,
                             sizeof(cloud_job_id));
    deneb_json_escape_string(status->req[0] ? status->req : deneb_status_state_name(status->state),
                             req, sizeof(req));
    deneb_json_escape_string(safe_value(status->firmware), firmware,
                             sizeof(firmware));
    deneb_json_escape_string(safe_value(status->machine_type), machine_type,
                             sizeof(machine_type));
    deneb_json_escape_string(status->error.detail, error_detail, sizeof(error_detail));
    deneb_json_escape_string(status->flow_last_response, flow_last_response,
                             sizeof(flow_last_response));

    n = snprintf(out, out_sz,
                 "{\"file\":\"%s\",\"name\":\"%s\",\"headTset\":%.1f,"
                 "\"headTcur\":%.1f,\"bedTset\":%.1f,\"bedTcur\":%.1f,"
                 "\"topcapIsPresent\":%s,\"topcapTemperature\":%.1f,"
                 "\"X\":%.3f,\"Y\":%.3f,\"Z\":%.3f,\"E\":%.3f,"
                 "\"homeDistanceX\":%.3f,\"homeDistanceY\":%.3f,"
                 "\"homeDistanceZ\":%.3f,\"homeDistanceValid\":%s,"
                 "\"flowInflight\":%u,\"flowSent\":%u,\"flowAck\":%u,"
                 "\"flowResend\":%u,\"flowReject\":%u,"
                 "\"flowLastResponse\":\"%s\","
                 "\"jobQueueDepth\":%u,\"jobLineNumber\":%u,"
                 "\"commandLatencyMs\":%u,\"plannerStarvationCount\":%u,"
                 "\"positionReportCount\":%u,"
                 "\"finishDrainTicks\":%u,\"finishStableReports\":%u,"
                 "\"denebState\":\"%s\",\"denebActive\":%s,"
                 "\"denebStopAllowed\":%s,"
                 "\"denebErrorKey\":\"%s\",\"denebErrorCategory\":\"%s\","
                 "\"denebErrorDetail\":\"%s\","
                 "\"firmware\":\"%s\",\"machineType\":\"%s\",\"pcbId\":%d,"
                 "\"pcbIdValid\":%s,"
                 "\"uuid\":\"%s\",\"source\":\"%s\",\"cloud_job_id\":\"%s\","
                 "\"Ttot\":%d,"
                 "\"Tleft\":%d,\"req\":\"%s\",\"received_faults\":%s}",
                 file, file,
                 status->head_t_set, status->head_t_cur,
                 status->bed_t_set, status->bed_t_cur,
                 status->topcap_present ? "true" : "false",
                 status->topcap_t_cur,
                 status->x, status->y, status->z, status->e,
                 status->home_x, status->home_y, status->home_z,
                 status->home_distance_valid ? "true" : "false",
                 status->flow_inflight, status->flow_sent, status->flow_ack,
                 status->flow_resend, status->flow_reject,
                 flow_last_response,
                 status->job_queue_depth, status->job_line_number,
                 status->command_latency_ms, status->planner_starvation_count,
                 status->position_report_count,
                 status->finish_drain_ticks, status->finish_stable_reports,
                 deneb_print_control_phase_name(
                     deneb_print_control_phase_from_state(status->state)),
                 deneb_status_has_active_print(status) ? "true" : "false",
                 deneb_print_control_phase_stop_allowed(
                     deneb_print_control_phase_from_state(status->state)) ? "true" : "false",
                 status->error.key ? status->error.key : DENEB_PRINT_NONE_VALUE,
                 status->error.category ? status->error.category : DENEB_PRINT_NONE_VALUE,
                 error_detail,
                 firmware, machine_type, status->pcb_id,
                 status->pcb_id_valid ? "true" : "false",
                 uuid, source, cloud_job_id,
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
