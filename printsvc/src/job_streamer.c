/* SPDX-License-Identifier: MPL-2.0 */
#include "job_streamer.h"

#include "config.h"
#include "error_map.h"
#include "job_lifecycle.h"
#include "motion_policy.h"
#include "motion_sender.h"

static int streamer_valid(const deneb_job_streamer_t *streamer)
{
    return streamer && streamer->status && streamer->flow && streamer->stream &&
           streamer->heater_wait && streamer->serial && streamer->job_active &&
           streamer->abort_requested && streamer->planner_starvation_count;
}

int deneb_job_streamer_poll(deneb_job_streamer_t *streamer)
{
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int rc;

    if (!streamer_valid(streamer))
        return -1;
    if (!*streamer->job_active)
        return 0;

    if (streamer->status->state == DENEB_PRINT_STATE_PAUSED)
        return 0;

    if (*streamer->abort_requested) {
        deneb_gcode_stream_close(streamer->stream);
        *streamer->job_active = 0;
        return -2;
    }

    if (deneb_heater_wait_ready(streamer->heater_wait, streamer->status)) {
        streamer->heater_wait->active = 0;
    } else if (streamer->heater_wait->active) {
        deneb_heater_wait_apply_status(streamer->heater_wait, streamer->status);
        return 0;
    }

    if (deneb_flow_inflight(streamer->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
        return 0;

    rc = deneb_gcode_stream_next(streamer->stream, line, sizeof(line));
    if (rc < 0) {
        deneb_gcode_stream_close(streamer->stream);
        *streamer->job_active = 0;
        deneb_job_lifecycle_error(streamer->status,
                                  deneb_error_make(DENEB_ERROR_STORAGE,
                                                   "job stream read failed"));
        return -1;
    }

    if (rc == 0) {
        deneb_motion_policy_t finish_policy;
        deneb_gcode_stream_close(streamer->stream);
        *streamer->job_active = 0;
        deneb_motion_policy_finish(&finish_policy);
        deneb_motion_sender_apply_policy(streamer->flow, streamer->serial,
                                         streamer->serial_ready,
                                         &finish_policy);
        deneb_job_lifecycle_complete(streamer->status);
        return 1;
    }

    deneb_job_lifecycle_streaming(streamer->status);
    if (deneb_flow_inflight(streamer->flow) == 0)
        (*streamer->planner_starvation_count)++;
    return deneb_motion_sender_send_gcode(streamer->flow, streamer->serial,
                                          streamer->serial_ready, line) == 0 ?
        1 : -1;
}
