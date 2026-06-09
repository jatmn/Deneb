/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MACRO_RUNNER_H
#define DENEB_PRINTSVC_MACRO_RUNNER_H

typedef struct {
    void *ctx;
    int (*abort_requested)(void *ctx);
    int (*wait_for_window)(void *ctx, long long timeout_ms);
    int (*send_gcode)(void *ctx, const char *line);
    int (*poll_motion)(void *ctx);
    int (*wait_for_heater)(void *ctx, int wait_bed, int wait_nozzle,
                           float target, long long timeout_ms);
} deneb_macro_runner_io_t;

int deneb_macro_runner_run_file(const char *path,
                                const deneb_macro_runner_io_t *io);
int deneb_macro_runner_run_macro(const char *macro,
                                 const deneb_macro_runner_io_t *io);

#endif
