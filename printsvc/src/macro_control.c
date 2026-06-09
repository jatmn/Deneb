/* SPDX-License-Identifier: MPL-2.0 */
#include "macro_control.h"

#include "command_reply.h"
#include "config.h"
#include "error_map.h"
#include "macro_runner.h"
#include "motion_send_error.h"
#include "motion_sender.h"

#include <time.h>
#include <unistd.h>

static long long macro_control_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int macro_control_wait_for_stream_window(deneb_print_service_t *svc,
                                                long long timeout_ms)
{
    long long deadline = macro_control_monotonic_ms() + timeout_ms;

    if (!svc)
        return -1;

    while (!deneb_flow_can_send(&svc->flow) ||
           deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW) {
        if (svc->abort_requested)
            return -2;
        if (deneb_print_service_poll_motion(svc) < 0)
            return svc->status.error.code == DENEB_ERROR_SERIAL ?
                       DENEB_MOTION_SEND_SERIAL :
                       -1;
        if (macro_control_monotonic_ms() >= deadline)
            return -1;
        usleep(10000);
    }

    return 0;
}

static int macro_control_abort_requested_cb(void *ctx)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    return svc && svc->abort_requested;
}

static int macro_control_wait_for_stream_window_cb(void *ctx,
                                                   long long timeout_ms)
{
    return macro_control_wait_for_stream_window((deneb_print_service_t *)ctx,
                                                timeout_ms);
}

static int macro_control_send_gcode_cb(void *ctx, const char *line)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    if (!svc)
        return -1;
    return deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                          svc->serial_ready, line);
}

static int macro_control_poll_motion_cb(void *ctx)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    int rc;

    if (!svc)
        return -1;

    rc = deneb_print_service_poll_motion(svc);
    if (rc < 0 && svc->status.error.code == DENEB_ERROR_SERIAL)
        return DENEB_MOTION_SEND_SERIAL;
    return rc < 0 ? rc : 0;
}

static int macro_control_wait_for_heater_cb(void *ctx, int wait_bed,
                                            int wait_nozzle, float target,
                                            long long timeout_ms)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;
    long long deadline = macro_control_monotonic_ms() + timeout_ms;

    if (!svc)
        return -1;

    if (wait_bed) {
        svc->status.bed_t_set = target;
        deneb_heater_wait_start_bed(&svc->heater_wait, target, 1.0f);
    } else if (wait_nozzle) {
        svc->status.head_t_set = target;
        deneb_heater_wait_start_head(&svc->heater_wait, target, 1.0f);
    } else {
        return 0;
    }

    while (!deneb_heater_wait_ready(&svc->heater_wait, &svc->status)) {
        int rc;
        deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
        if (svc->abort_requested)
            return -2;
        rc = macro_control_poll_motion_cb(svc);
        if (rc != 0)
            return rc;
        if (macro_control_monotonic_ms() >= deadline)
            return -1;
        usleep(10000);
    }

    svc->heater_wait.active = 0;
    return 0;
}

int deneb_macro_control_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz)
{
    deneb_macro_runner_io_t io;

    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    io.ctx = svc;
    io.abort_requested = macro_control_abort_requested_cb;
    io.wait_for_window = macro_control_wait_for_stream_window_cb;
    io.send_gcode = macro_control_send_gcode_cb;
    io.poll_motion = macro_control_poll_motion_cb;
    io.wait_for_heater = macro_control_wait_for_heater_cb;
    int rc = deneb_macro_runner_run_macro(cmd->macro, &io);
    if (rc != 0) {
        svc->status.error =
            deneb_error_make(deneb_motion_send_error_code(rc),
                             "macro failed");
        deneb_command_reply_error(reply, reply_sz, "macro failed");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "macro complete");
    return 0;
}
