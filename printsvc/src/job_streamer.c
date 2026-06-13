/* SPDX-License-Identifier: MPL-2.0 */
#include "job_streamer.h"

#include "config.h"
#include "error_map.h"
#include "job_lifecycle.h"
#include "motion_policy.h"
#include "motion_send_error.h"
#include "motion_sender.h"

#include <stdio.h>

enum {
    DENEB_JOB_PREPARE_DONE = 0,
    DENEB_JOB_PREPARE_HOME = 1,
    DENEB_JOB_PREPARE_HOME_DRAIN = 2,
    DENEB_JOB_PREPARE_HEAT = 3,
    DENEB_JOB_PREPARE_Z_HOME = 4,
    DENEB_JOB_PREPARE_Z_DRAIN = 5,
    DENEB_JOB_PREPARE_STARTUP = 6,
    DENEB_JOB_PREPARE_STARTUP_DRAIN = 7
};

static const char *const DENEB_JOB_PREPARE_HOME_COMMANDS[] = {
    "G28",
    "G0 X105 Y0 F9000",
    "M18 Z",
};

static const char *const DENEB_JOB_PREPARE_Z_HOME_COMMANDS[] = {
    "G28 Z",
};

static const char *const DENEB_JOB_PRIME_FILAMENT_COMMANDS[] = {
    "G10 S-6.5 F1500",
    "G10 S0 F300",
};

static const char *const DENEB_JOB_STARTUP_COMMANDS[] = {
    "G90",
    "M82",
    "G92 E0",
    "G0 F9000",
};

static int streamer_valid(const deneb_job_streamer_t *streamer)
{
    return streamer && streamer->status && streamer->flow && streamer->stream &&
           streamer->heater_wait && streamer->serial && streamer->job_active &&
           streamer->serial_ready && streamer->job_prepare_stage &&
           streamer->job_prepare_index && streamer->job_startup_index &&
           streamer->abort_requested &&
           streamer->finish_cleanup_pending &&
           streamer->finish_cleanup_policy && streamer->finish_cleanup_index &&
           streamer->planner_starvation_count;
}

static int send_command_list(deneb_job_streamer_t *streamer,
                             const char *const *commands,
                             size_t command_count,
                             size_t *index)
{
    int sent_any = 0;

    while (*index < command_count) {
        int rc;
        if (deneb_flow_has_pending_barrier(streamer->flow) ||
            !deneb_flow_can_send(streamer->flow) ||
            deneb_flow_inflight(streamer->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return sent_any ? 1 : 0;
        rc = deneb_motion_sender_send_gcode(streamer->flow, streamer->serial,
                                            *streamer->serial_ready,
                                            commands[*index]);
        if (rc != 0) {
            char detail[96];
            if (rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            snprintf(detail, sizeof(detail), "job prepare send failed: %s",
                     deneb_motion_send_error_name(rc));
            deneb_job_lifecycle_error(
                streamer->status,
                deneb_error_make(deneb_motion_send_error_code(rc), detail));
            return -1;
        }
        (*index)++;
        sent_any = 1;
    }
    return 2;
}

static int send_job_startup(deneb_job_streamer_t *streamer)
{
    size_t prime_count = sizeof(DENEB_JOB_PRIME_FILAMENT_COMMANDS) /
                         sizeof(DENEB_JOB_PRIME_FILAMENT_COMMANDS[0]);
    size_t startup_count = sizeof(DENEB_JOB_STARTUP_COMMANDS) /
                           sizeof(DENEB_JOB_STARTUP_COMMANDS[0]);

    if (!streamer->stream->has_prime_cmd &&
        *streamer->job_startup_index < prime_count) {
        int rc = send_command_list(streamer, DENEB_JOB_PRIME_FILAMENT_COMMANDS,
                                   prime_count,
                                   streamer->job_startup_index);
        if (rc <= 0)
            return rc;
        if (rc == 1)
            return 1;
    }

    if (!streamer->stream->has_prime_cmd)
        *streamer->job_startup_index -= prime_count;

    {
        int rc = send_command_list(streamer, DENEB_JOB_STARTUP_COMMANDS,
                                   startup_count,
                                   streamer->job_startup_index);
        if (!streamer->stream->has_prime_cmd)
            *streamer->job_startup_index += prime_count;
        return rc;
    }
}

static int poll_job_prepare(deneb_job_streamer_t *streamer)
{
    int rc;

    switch (*streamer->job_prepare_stage) {
        case DENEB_JOB_PREPARE_HOME:
            rc = send_command_list(
                streamer, DENEB_JOB_PREPARE_HOME_COMMANDS,
                sizeof(DENEB_JOB_PREPARE_HOME_COMMANDS) /
                    sizeof(DENEB_JOB_PREPARE_HOME_COMMANDS[0]),
                streamer->job_prepare_index);
            if (rc <= 0)
                return rc;
            if (rc == 1)
                return 1;
            *streamer->job_prepare_index = 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_HOME_DRAIN;
            return 1;

        case DENEB_JOB_PREPARE_HOME_DRAIN:
            if (deneb_flow_inflight(streamer->flow) != 0)
                return 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_HEAT;
            return 0;

        case DENEB_JOB_PREPARE_HEAT:
            if (deneb_heater_wait_ready(streamer->heater_wait,
                                        streamer->status)) {
                streamer->heater_wait->active = 0;
                *streamer->job_prepare_stage = DENEB_JOB_PREPARE_Z_HOME;
                return 0;
            }
            if (streamer->heater_wait->active) {
                deneb_heater_wait_apply_status(streamer->heater_wait,
                                               streamer->status);
                return 0;
            }
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_Z_HOME;
            return 0;

        case DENEB_JOB_PREPARE_Z_HOME:
            rc = send_command_list(
                streamer, DENEB_JOB_PREPARE_Z_HOME_COMMANDS,
                sizeof(DENEB_JOB_PREPARE_Z_HOME_COMMANDS) /
                    sizeof(DENEB_JOB_PREPARE_Z_HOME_COMMANDS[0]),
                streamer->job_prepare_index);
            if (rc <= 0)
                return rc;
            if (rc == 1)
                return 1;
            *streamer->job_prepare_index = 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_Z_DRAIN;
            return 1;

        case DENEB_JOB_PREPARE_Z_DRAIN:
            if (deneb_flow_inflight(streamer->flow) != 0)
                return 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_STARTUP;
            return 0;

        case DENEB_JOB_PREPARE_STARTUP:
            rc = send_job_startup(streamer);
            if (rc <= 0)
                return rc;
            if (rc == 1)
                return 1;
            *streamer->job_startup_index = 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_STARTUP_DRAIN;
            return 1;

        case DENEB_JOB_PREPARE_STARTUP_DRAIN:
            if (deneb_flow_inflight(streamer->flow) != 0)
                return 0;
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_DONE;
            return 0;

        default:
            *streamer->job_prepare_stage = DENEB_JOB_PREPARE_DONE;
            return 0;
    }
}

int deneb_job_streamer_poll(deneb_job_streamer_t *streamer)
{
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int rc;

    if (!streamer_valid(streamer))
        return -1;
    if (!*streamer->job_active) {
        *streamer->abort_requested = 0;
        streamer->heater_wait->active = 0;
        return 0;
    }

    if (streamer->status->state == DENEB_PRINT_STATE_ERROR) {
        deneb_gcode_stream_close(streamer->stream);
        deneb_flow_clear_inflight(streamer->flow);
        streamer->stream->line_number = 0;
        *streamer->job_active = 0;
        *streamer->finish_cleanup_pending = 0;
        *streamer->abort_requested = 0;
        *streamer->job_prepare_stage = DENEB_JOB_PREPARE_DONE;
        *streamer->job_prepare_index = 0;
        *streamer->job_startup_index = 0;
        streamer->heater_wait->active = 0;
        return -1;
    }

    if (streamer->status->state == DENEB_PRINT_STATE_PAUSED)
        return 0;

    if (*streamer->finish_cleanup_pending) {
        deneb_job_lifecycle_streaming(streamer->status);
        return 0;
    }

    if (*streamer->abort_requested) {
        deneb_gcode_stream_close(streamer->stream);
        deneb_flow_clear_inflight(streamer->flow);
        streamer->stream->line_number = 0;
        *streamer->job_active = 0;
        *streamer->finish_cleanup_pending = 0;
        deneb_job_lifecycle_abort(streamer->status);
        *streamer->abort_requested = 0;
        *streamer->job_prepare_stage = DENEB_JOB_PREPARE_DONE;
        *streamer->job_prepare_index = 0;
        *streamer->job_startup_index = 0;
        streamer->heater_wait->active = 0;
        return -2;
    }

    if (*streamer->job_prepare_stage != DENEB_JOB_PREPARE_DONE)
        return poll_job_prepare(streamer);

    if (deneb_heater_wait_ready(streamer->heater_wait, streamer->status)) {
        streamer->heater_wait->active = 0;
    } else if (streamer->heater_wait->active) {
        deneb_heater_wait_apply_status(streamer->heater_wait, streamer->status);
        return 0;
    }

    if (deneb_flow_has_pending_barrier(streamer->flow) ||
        !deneb_flow_can_send(streamer->flow) ||
        deneb_flow_inflight(streamer->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
        return 0;

    rc = deneb_gcode_stream_next(streamer->stream, line, sizeof(line));
    if (rc < 0) {
        deneb_gcode_stream_close(streamer->stream);
        deneb_flow_clear_inflight(streamer->flow);
        streamer->stream->line_number = 0;
        *streamer->job_active = 0;
        *streamer->finish_cleanup_pending = 0;
        streamer->heater_wait->active = 0;
        deneb_job_lifecycle_error(streamer->status,
                                  deneb_error_make(DENEB_ERROR_STORAGE,
                                                   "job stream read failed"));
        return -1;
    }

    if (rc == 0) {
        if (deneb_flow_inflight(streamer->flow) != 0) {
            deneb_job_lifecycle_streaming(streamer->status);
            return 0;
        }
        deneb_gcode_stream_close(streamer->stream);
        streamer->heater_wait->active = 0;
        deneb_motion_policy_finish(streamer->finish_cleanup_policy);
        *streamer->finish_cleanup_index = 0;
        *streamer->finish_cleanup_pending = 1;
        deneb_job_lifecycle_streaming(streamer->status);
        return 1;
    }

    deneb_job_lifecycle_streaming(streamer->status);
    if (deneb_flow_inflight(streamer->flow) == 0)
        (*streamer->planner_starvation_count)++;
    rc = deneb_motion_sender_send_gcode(streamer->flow, streamer->serial,
                                        *streamer->serial_ready, line);
    if (rc != 0) {
        char detail[96];
        if (rc == DENEB_MOTION_SEND_FLOW_FULL)
            return 0;
        deneb_gcode_stream_close(streamer->stream);
        deneb_flow_clear_inflight(streamer->flow);
        streamer->stream->line_number = 0;
        *streamer->job_active = 0;
        *streamer->finish_cleanup_pending = 0;
        streamer->heater_wait->active = 0;
        snprintf(detail, sizeof(detail), "job stream send failed: %s",
                 deneb_motion_send_error_name(rc));
        deneb_job_lifecycle_error(streamer->status,
                                  deneb_error_make(
                                      deneb_motion_send_error_code(rc),
                                      detail));
        return -1;
    }
    {
        int wait_bed = 0;
        int wait_nozzle = 0;
        float wait_target = 0.0f;
        if (deneb_gcode_stream_last_wait(streamer->stream, &wait_bed,
                                         &wait_nozzle, &wait_target) > 0) {
            if (wait_bed) {
                streamer->status->bed_t_set = wait_target;
                deneb_heater_wait_start_bed(streamer->heater_wait, wait_target,
                                            1.0f);
            } else if (wait_nozzle) {
                streamer->status->head_t_set = wait_target;
                deneb_heater_wait_start_head(streamer->heater_wait, wait_target,
                                             1.0f);
            }
            deneb_heater_wait_apply_status(streamer->heater_wait,
                                           streamer->status);
        }
    }
    return 1;
}
