/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_JOB_STREAMER_H
#define DENEB_PRINTSVC_JOB_STREAMER_H

#include "flow_control.h"
#include "gcode_stream.h"
#include "heater_wait.h"
#include "motion_policy.h"
#include "serial_transport.h"
#include "status.h"

#include <time.h>

typedef struct {
    deneb_status_t *status;
    deneb_flow_control_t *flow;
    deneb_gcode_stream_t *stream;
    deneb_heater_wait_t *heater_wait;
    deneb_serial_transport_t *serial;
    int *serial_ready;
    int *job_active;
    time_t *job_started_at;
    int *job_prepare_stage;
    size_t *job_prepare_index;
    size_t *job_startup_index;
    int *abort_requested;
    int *finish_cleanup_pending;
    deneb_motion_policy_t *finish_cleanup_policy;
    size_t *finish_cleanup_index;
    unsigned int *planner_starvation_count;
    float *job_nozzle_resume_setpoint;
} deneb_job_streamer_t;

int deneb_job_streamer_poll(deneb_job_streamer_t *streamer);

#endif
