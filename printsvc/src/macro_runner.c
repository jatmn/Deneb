/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "gcode_stream.h"
#include "macro_registry.h"
#include "macro_runner.h"

int deneb_macro_runner_run_file(const char *path,
                                const deneb_macro_runner_io_t *io)
{
    deneb_gcode_stream_t stream;
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int rc;

    if (!io || !io->wait_for_window || !io->send_gcode)
        return -1;

    if (deneb_gcode_stream_open(&stream, path) != 0)
        return -1;

    while ((rc = deneb_gcode_stream_next(&stream, line, sizeof(line))) > 0) {
        if (io->abort_requested && io->abort_requested(io->ctx)) {
            deneb_gcode_stream_close(&stream);
            return -2;
        }
        rc = io->wait_for_window(io->ctx, 5000);
        if (rc != 0) {
            deneb_gcode_stream_close(&stream);
            return rc;
        }
        rc = io->send_gcode(io->ctx, line);
        if (rc != 0) {
            deneb_gcode_stream_close(&stream);
            return rc;
        }
        if (io->poll_motion) {
            rc = io->poll_motion(io->ctx);
            if (rc != 0) {
                deneb_gcode_stream_close(&stream);
                return rc;
            }
        }
    }

    deneb_gcode_stream_close(&stream);
    return rc < 0 ? -1 : 0;
}

int deneb_macro_runner_run_macro(const char *macro,
                                 const deneb_macro_runner_io_t *io)
{
    char path[256];

    if (deneb_macro_resolve(macro, path, sizeof(path)) != 0)
        return -1;
    return deneb_macro_runner_run_file(path, io);
}
