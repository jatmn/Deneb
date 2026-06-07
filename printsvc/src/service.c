/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "diagnostics_log.h"
#include "gcode_stream.h"
#include "macro_registry.h"
#include "motion_policy.h"
#include "print_control.h"
#include "print_state_rules.h"
#include "service.h"
#include "status_parser.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void deneb_print_service_init(deneb_print_service_t *svc)
{
    memset(svc, 0, sizeof(*svc));
    deneb_status_init(&svc->status);
    deneb_flow_init(&svc->flow);
    deneb_heater_wait_init(&svc->heater_wait);
    svc->serial.fd = -1;
}

int deneb_print_service_open_motion(deneb_print_service_t *svc)
{
    if (!svc)
        return -1;

    if (deneb_serial_open(&svc->serial, DENEB_PRINTSVC_SERIAL_DEVICE,
                          DENEB_PRINTSVC_SERIAL_BAUD) == 0) {
        svc->serial_ready = 1;
        return 0;
    }

    svc->serial_ready = 0;
    return -1;
}

static int service_send_gcode(deneb_print_service_t *svc, const char *line)
{
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;
    uint8_t sequence = 0;

    if (!svc || !line || !*line)
        return -1;

    if (deneb_flow_prepare_packet(&svc->flow, line, packet, sizeof(packet),
                                  &written, &sequence) != 0)
        return -1;

    if (!svc->serial_ready)
        return 0;

    return deneb_serial_write_all(&svc->serial, packet, written);
}

static int service_resend_sequence(deneb_print_service_t *svc, uint8_t sequence)
{
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;

    if (deneb_flow_get_resend_packet(&svc->flow, sequence, packet,
                                     sizeof(packet), &written) != 0)
        return -1;

    if (!svc->serial_ready)
        return 0;

    return deneb_serial_write_all(&svc->serial, packet, written);
}

int deneb_print_service_poll_motion(deneb_print_service_t *svc)
{
    char line[DENEB_PRINTSVC_SERIAL_LINE];
    uint8_t resend_sequence = 0;
    int handled = 0;

    if (!svc)
        return -1;
    if (!svc->serial_ready)
        return 0;

    for (;;) {
        int n = deneb_serial_read_line(&svc->serial, line, sizeof(line));
        if (n < 0)
            return -1;
        if (n == 0)
            break;

        handled++;
        deneb_status_parse_marlin_line(&svc->status, line);
        if (deneb_heater_wait_ready(&svc->heater_wait, &svc->status)) {
            svc->heater_wait.active = 0;
        } else {
            deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
        }
        int flow_rc = deneb_flow_handle_response(&svc->flow, line,
                                                 &resend_sequence);
        if (flow_rc == 2) {
            service_resend_sequence(svc, resend_sequence);
        }
    }

    return handled;
}

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void deneb_print_service_refresh_diagnostics(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    svc->status.flow_inflight = (unsigned int)deneb_flow_inflight(&svc->flow);
    svc->status.flow_sent = svc->flow.sent_count;
    svc->status.flow_ack = svc->flow.ack_count;
    svc->status.flow_resend = svc->flow.resend_count;
    svc->status.flow_reject = svc->flow.reject_count;
    svc->status.job_queue_depth = svc->job_active ? 1u : 0u;
    svc->status.job_line_number = svc->job_active ?
        (unsigned int)svc->job_stream.line_number : 0u;
    svc->status.planner_starvation_count = svc->planner_starvation_count;
}

static int service_wait_for_stream_window(deneb_print_service_t *svc, long long timeout_ms)
{
    long long deadline = monotonic_ms() + timeout_ms;

    while (deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW) {
        if (svc->abort_requested)
            return -2;
        if (deneb_print_service_poll_motion(svc) < 0)
            return -1;
        if (monotonic_ms() >= deadline)
            return -1;
        usleep(10000);
    }

    return 0;
}

static int service_stream_file_blocking(deneb_print_service_t *svc, const char *path)
{
    deneb_gcode_stream_t stream;
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int rc;

    if (deneb_gcode_stream_open(&stream, path) != 0)
        return -1;

    while ((rc = deneb_gcode_stream_next(&stream, line, sizeof(line))) > 0) {
        if (svc->abort_requested) {
            deneb_gcode_stream_close(&stream);
            return -2;
        }
        rc = service_wait_for_stream_window(svc, 5000);
        if (rc != 0) {
            deneb_gcode_stream_close(&stream);
            return rc;
        }
        if (service_send_gcode(svc, line) != 0) {
            deneb_gcode_stream_close(&stream);
            return -1;
        }
        deneb_print_service_poll_motion(svc);
    }

    deneb_gcode_stream_close(&stream);
    return rc < 0 ? -1 : 0;
}

static int service_apply_motion_policy(deneb_print_service_t *svc,
                                       const deneb_motion_policy_t *policy)
{
    if (!svc || !policy)
        return -1;

    for (size_t i = 0; i < policy->count; i++) {
        if (service_send_gcode(svc, policy->commands[i]) != 0)
            return -1;
    }
    return 0;
}

static void reply_json(char *reply, size_t reply_sz, const char *status, const char *message)
{
    snprintf(reply, reply_sz, "{\"status\":\"%s\",\"message\":\"%s\"}",
             status, message ? message : "");
}

static int handle_job(deneb_print_service_t *svc, const deneb_command_t *cmd,
                      char *reply, size_t reply_sz)
{
    if (!cmd->file[0]) {
        reply_json(reply, reply_sz, "error", "missing job file");
        return -1;
    }

    if (svc->job_active) {
        reply_json(reply, reply_sz, "error", "job already active");
        return -1;
    }

    if (deneb_gcode_stream_open(&svc->job_stream, cmd->file) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_STORAGE, "failed to open job file");
        reply_json(reply, reply_sz, "error", "failed to open job file");
        return -1;
    }

    svc->abort_requested = 0;
    svc->job_active = 1;
    svc->status.state = DENEB_PRINT_STATE_PREPARING;
    snprintf(svc->status.req, sizeof(svc->status.req), "%s",
             deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PREPARING));
    snprintf(svc->status.file, sizeof(svc->status.file), "%s", cmd->file);
    snprintf(svc->status.source, sizeof(svc->status.source), "%s",
             cmd->source[0] ? cmd->source : DENEB_PRINT_USB_JOB_SOURCE);
    snprintf(svc->status.uuid, sizeof(svc->status.uuid), "%s", cmd->uuid);
    if (cmd->bed_target > 0.0f)
        svc->status.bed_t_set = cmd->bed_target;
    if (cmd->head_target > 0.0f)
        svc->status.head_t_set = cmd->head_target;
    deneb_heater_wait_start(&svc->heater_wait, svc->status.bed_t_set,
                            svc->status.head_t_set, 1.0f);

    reply_json(reply, reply_sz, "ok", "job accepted");
    return 0;
}

static int handle_macro(deneb_print_service_t *svc, const deneb_command_t *cmd,
                        char *reply, size_t reply_sz)
{
    char path[256];

    if (deneb_macro_resolve(cmd->macro, path, sizeof(path)) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "invalid macro");
        reply_json(reply, reply_sz, "error", "invalid macro");
        return -1;
    }

    if (service_stream_file_blocking(svc, path) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "macro failed");
        reply_json(reply, reply_sz, "error", "macro failed");
        return -1;
    }

    reply_json(reply, reply_sz, "ok", "macro complete");
    return 0;
}

static int service_handle_command_inner(deneb_print_service_t *svc,
                                        const deneb_command_t *cmd,
                                        char *reply, size_t reply_sz)
{
    switch (cmd->type) {
        case DENEB_COMMAND_GCODE:
            for (size_t i = 0; i < cmd->gcode_count; i++) {
                if (service_send_gcode(svc, cmd->gcode[i]) != 0) {
                    svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "gcode failed");
                    reply_json(reply, reply_sz, "error", "gcode failed");
                    return -1;
                }
            }
            reply_json(reply, reply_sz, "ok", "gcode accepted");
            return 0;

        case DENEB_COMMAND_MACRO:
            return handle_macro(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_JOB:
            return handle_job(svc, cmd, reply, reply_sz);

        case DENEB_COMMAND_ABORT:
            {
            deneb_motion_policy_t abort_policy;
            deneb_motion_policy_abort(&abort_policy);
            if (svc->job_active) {
                deneb_gcode_stream_close(&svc->job_stream);
                svc->job_active = 0;
            }
            service_apply_motion_policy(svc, &abort_policy);
            svc->abort_requested = 1;
            svc->status.state = DENEB_PRINT_STATE_IDLE;
            snprintf(svc->status.file, sizeof(svc->status.file), "none");
            snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                     deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_IDLE));
            svc->status.time_total = 0;
            svc->status.time_left = 0;
            reply_json(reply, reply_sz, "ok", "abort accepted");
            return 0;
            }

        case DENEB_COMMAND_PAUSE:
            if (deneb_status_has_active_print(&svc->status)) {
                svc->status.state = DENEB_PRINT_STATE_PAUSED;
                snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                         deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PAUSED));
            }
            reply_json(reply, reply_sz, "ok", "pause accepted");
            return 0;

        case DENEB_COMMAND_RESUME:
            if (svc->status.state == DENEB_PRINT_STATE_PAUSED) {
                svc->status.state = svc->heater_wait.active ?
                    DENEB_PRINT_STATE_PREPARING : DENEB_PRINT_STATE_PRINTING;
                snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                         deneb_print_control_req_for_phase(
                             svc->heater_wait.active ?
                             DENEB_PRINT_PHASE_PREPARING :
                             DENEB_PRINT_PHASE_PRINTING));
            }
            reply_json(reply, reply_sz, "ok", "resume accepted");
            return 0;

        default:
            reply_json(reply, reply_sz, "error", "unknown command");
            return -1;
    }
}

int deneb_print_service_handle_command(deneb_print_service_t *svc,
                                       const deneb_command_t *cmd,
                                       char *reply, size_t reply_sz)
{
    long long start_ms;
    long long elapsed_ms;
    int rc;

    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    start_ms = monotonic_ms();
    rc = service_handle_command_inner(svc, cmd, reply, reply_sz);
    elapsed_ms = monotonic_ms() - start_ms;
    if (elapsed_ms < 0)
        elapsed_ms = 0;
    if (elapsed_ms > 60000)
        elapsed_ms = 60000;
    svc->status.command_latency_ms = (unsigned int)elapsed_ms;
    deneb_print_service_refresh_diagnostics(svc);
    deneb_diagnostics_log_command(cmd, rc, svc->status.command_latency_ms);
    deneb_diagnostics_log_status(&svc->status, 1);
    return rc;
}

int deneb_print_service_poll_job(deneb_print_service_t *svc)
{
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int rc;

    if (!svc || !svc->job_active)
        return 0;

    if (svc->status.state == DENEB_PRINT_STATE_PAUSED)
        return 0;

    if (svc->abort_requested) {
        deneb_gcode_stream_close(&svc->job_stream);
        svc->job_active = 0;
        return -2;
    }

    if (deneb_heater_wait_ready(&svc->heater_wait, &svc->status)) {
        svc->heater_wait.active = 0;
    } else if (svc->heater_wait.active) {
        deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
        return 0;
    }

    if (deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
        return 0;

    rc = deneb_gcode_stream_next(&svc->job_stream, line, sizeof(line));
    if (rc < 0) {
        deneb_gcode_stream_close(&svc->job_stream);
        svc->job_active = 0;
        svc->status.state = DENEB_PRINT_STATE_ERROR;
        svc->status.fault = true;
        svc->status.error = deneb_error_make(DENEB_ERROR_STORAGE, "job stream read failed");
        snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                 deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_ERROR));
        return -1;
    }

    if (rc == 0) {
        deneb_motion_policy_t finish_policy;
        deneb_gcode_stream_close(&svc->job_stream);
        svc->job_active = 0;
        deneb_motion_policy_finish(&finish_policy);
        service_apply_motion_policy(svc, &finish_policy);
        svc->status.state = DENEB_PRINT_STATE_COMPLETE;
        snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                 deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_COMPLETE));
        return 1;
    }

    svc->status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc->status.req, sizeof(svc->status.req), "%s",
             deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PRINTING));
    if (deneb_flow_inflight(&svc->flow) == 0)
        svc->planner_starvation_count++;
    return service_send_gcode(svc, line) == 0 ? 1 : -1;
}

void deneb_print_service_close(deneb_print_service_t *svc)
{
    if (!svc)
        return;
    deneb_gcode_stream_close(&svc->job_stream);
    svc->job_active = 0;
    deneb_serial_close(&svc->serial);
    svc->serial_ready = 0;
}
