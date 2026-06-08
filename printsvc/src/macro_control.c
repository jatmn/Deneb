/* SPDX-License-Identifier: MPL-2.0 */
#include "macro_control.h"

#include "command_reply.h"
#include "config.h"
#include "error_map.h"
#include "macro_runner.h"
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

    while (deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW) {
        if (svc->abort_requested)
            return -2;
        if (deneb_print_service_poll_motion(svc) < 0)
            return -1;
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
    return deneb_print_service_poll_motion((deneb_print_service_t *)ctx);
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
    if (deneb_macro_runner_run_macro(cmd->macro, &io) != 0) {
        svc->status.error = deneb_error_make(DENEB_ERROR_COMMAND,
                                             "macro failed");
        deneb_command_reply_error(reply, reply_sz, "macro failed");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "macro complete");
    return 0;
}
