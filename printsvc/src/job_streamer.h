/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_JOB_STREAMER_H
#define DENEB_PRINTSVC_JOB_STREAMER_H

#include "flow_control.h"
#include "gcode_stream.h"
#include "heater_wait.h"
#include "serial_transport.h"
#include "status.h"

typedef struct {
    deneb_status_t *status;
    deneb_flow_control_t *flow;
    deneb_gcode_stream_t *stream;
    deneb_heater_wait_t *heater_wait;
    deneb_serial_transport_t *serial;
    int *serial_ready;
    int *job_active;
    int *abort_requested;
    unsigned int *planner_starvation_count;
} deneb_job_streamer_t;

int deneb_job_streamer_poll(deneb_job_streamer_t *streamer);

#endif
