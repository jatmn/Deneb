/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "command_reply.h"
#include "diagnostics_log.h"
#include "job_lifecycle.h"
#include "macro_runner.h"
#include "motion_observer.h"
#include "motion_policy.h"
#include "motion_sender.h"
#include "pause_resume.h"
#include "print_control.h"
#include "print_state_rules.h"
#include "runtime_diagnostics.h"
#include "service.h"

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
        int flow_rc = deneb_motion_observer_handle_line(
            &svc->status, &svc->heater_wait, &svc->flow, line,
            &resend_sequence);
        if (flow_rc == 2) {
            deneb_motion_sender_resend_sequence(&svc->flow, &svc->serial,
                                                svc->serial_ready,
                                                resend_sequence);
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

    deneb_runtime_diagnostics_refresh(
        &svc->status, &svc->flow, svc->job_active,
        (unsigned int)svc->job_stream.line_number,
        svc->planner_starvation_count);
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

static int service_abort_requested_cb(void *ctx)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    return svc && svc->abort_requested;
}

static int service_wait_for_stream_window_cb(void *ctx, long long timeout_ms)
{
    return service_wait_for_stream_window((deneb_print_service_t *)ctx,
                                          timeout_ms);
}

static int service_send_gcode_cb(void *ctx, const char *line)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    if (!svc)
        return -1;
    return deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                          svc->serial_ready, line);
}

static int service_poll_motion_cb(void *ctx)
{
    return deneb_print_service_poll_motion((deneb_print_service_t *)ctx);
}

static int handle_job(deneb_print_service_t *svc, const deneb_command_t *cmd,
                      char *reply, size_t reply_sz)
{
    if (!cmd->file[0]) {
        deneb_command_reply_error(reply, reply_sz, "missing job file");
        return -1;
    }

    if (svc->job_active) {
        deneb_command_reply_error(reply, reply_sz, "job already active");
        return -1;
    }

    if (deneb_gcode_stream_open(&svc->job_stream, cmd->file) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_STORAGE, "failed to open job file");
        deneb_command_reply_error(reply, reply_sz, "failed to open job file");
        return -1;
    }

    svc->abort_requested = 0;
    svc->job_active = 1;
    deneb_job_lifecycle_start(&svc->status, cmd->file, cmd->source,
                              cmd->uuid, cmd->bed_target, cmd->head_target);
    deneb_heater_wait_start(&svc->heater_wait, svc->status.bed_t_set,
                            svc->status.head_t_set, 1.0f);

    deneb_command_reply_ok(reply, reply_sz, "job accepted");
    return 0;
}

static int handle_macro(deneb_print_service_t *svc, const deneb_command_t *cmd,
                        char *reply, size_t reply_sz)
{
    deneb_macro_runner_io_t io;

    io.ctx = svc;
    io.abort_requested = service_abort_requested_cb;
    io.wait_for_window = service_wait_for_stream_window_cb;
    io.send_gcode = service_send_gcode_cb;
    io.poll_motion = service_poll_motion_cb;
    if (deneb_macro_runner_run_macro(cmd->macro, &io) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "macro failed");
        deneb_command_reply_error(reply, reply_sz, "macro failed");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "macro complete");
    return 0;
}

static int service_handle_command_inner(deneb_print_service_t *svc,
                                        const deneb_command_t *cmd,
                                        char *reply, size_t reply_sz)
{
    switch (cmd->type) {
        case DENEB_COMMAND_GCODE:
            for (size_t i = 0; i < cmd->gcode_count; i++) {
                if (deneb_motion_sender_send_gcode(&svc->flow,
                                                   &svc->serial,
                                                   svc->serial_ready,
                                                   cmd->gcode[i]) != 0) {
                    svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND, "gcode failed");
                    deneb_command_reply_error(reply, reply_sz, "gcode failed");
                    return -1;
                }
            }
            deneb_command_reply_ok(reply, reply_sz, "gcode accepted");
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
            deneb_motion_sender_apply_policy(&svc->flow, &svc->serial,
                                             svc->serial_ready,
                                             &abort_policy);
            svc->abort_requested = 1;
            deneb_job_lifecycle_abort(&svc->status);
            deneb_command_reply_ok(reply, reply_sz, "abort accepted");
            return 0;
            }

        case DENEB_COMMAND_PAUSE:
            if (deneb_pause_resume_pause(&svc->status) < 0) {
                deneb_command_reply_error(reply, reply_sz,
                                          "no active print to pause");
                return -1;
            }
            deneb_command_reply_ok(reply, reply_sz, "pause accepted");
            return 0;

        case DENEB_COMMAND_RESUME:
            if (deneb_pause_resume_resume(&svc->status,
                                          svc->heater_wait.active) < 0) {
                deneb_command_reply_error(reply, reply_sz,
                                          "print is not paused");
                return -1;
            }
            deneb_command_reply_ok(reply, reply_sz, "resume accepted");
            return 0;

        default:
            deneb_command_reply_error(reply, reply_sz, "unknown command");
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
        deneb_job_lifecycle_error(&svc->status,
                                  deneb_error_make(DENEB_ERROR_STORAGE,
                                                   "job stream read failed"));
        return -1;
    }

    if (rc == 0) {
        deneb_motion_policy_t finish_policy;
        deneb_gcode_stream_close(&svc->job_stream);
        svc->job_active = 0;
        deneb_motion_policy_finish(&finish_policy);
        deneb_motion_sender_apply_policy(&svc->flow, &svc->serial,
                                         svc->serial_ready,
                                         &finish_policy);
        deneb_job_lifecycle_complete(&svc->status);
        return 1;
    }

    deneb_job_lifecycle_streaming(&svc->status);
    if (deneb_flow_inflight(&svc->flow) == 0)
        svc->planner_starvation_count++;
    return deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                          svc->serial_ready, line) == 0 ?
        1 : -1;
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
