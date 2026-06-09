/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SERVICE_H
#define DENEB_PRINTSVC_SERVICE_H

#include "command.h"
#include "flow_control.h"
#include "gcode_stream.h"
#include "heater_wait.h"
#include "motion_policy.h"
#include "serial_transport.h"
#include "status.h"

typedef struct {
    deneb_status_t status;
    deneb_serial_transport_t serial;
    deneb_flow_control_t flow;
    deneb_gcode_stream_t job_stream;
    deneb_heater_wait_t heater_wait;
    char gcode_queue[DENEB_PRINTSVC_GCODE_QUEUE_COMMANDS][DENEB_PRINTSVC_MAX_GCODE_LINE];
    int gcode_queue_wait_bed[DENEB_PRINTSVC_GCODE_QUEUE_COMMANDS];
    int gcode_queue_wait_nozzle[DENEB_PRINTSVC_GCODE_QUEUE_COMMANDS];
    float gcode_queue_wait_target[DENEB_PRINTSVC_GCODE_QUEUE_COMMANDS];
    size_t gcode_queue_count;
    size_t gcode_queue_index;
    int gcode_queue_active;
    int serial_ready;
    int job_active;
    int abort_requested;
    int abort_cleanup_pending;
    deneb_motion_policy_t abort_cleanup_policy;
    size_t abort_cleanup_index;
    int finish_cleanup_pending;
    deneb_motion_policy_t finish_cleanup_policy;
    size_t finish_cleanup_index;
    unsigned int finish_drain_ticks;
    unsigned int finish_position_report_count;
    unsigned int finish_stable_reports;
    float finish_last_x;
    float finish_last_y;
    float finish_last_z;
    int pause_policy_pending;
    deneb_motion_policy_t pause_policy;
    size_t pause_policy_index;
    int pause_position_probe_pending;
    int pause_position_probe_sent;
    unsigned int pause_position_report_start;
    int resume_policy_pending;
    deneb_motion_policy_t resume_policy;
    size_t resume_policy_index;
    int paused_position_valid;
    float paused_x;
    float paused_y;
    float paused_z;
    float paused_e;
    float paused_r0;
    float paused_nozzle_setpoint;
    unsigned int planner_starvation_count;
    unsigned int temperature_poll_ticks;
    int startup_status_probe_pending;
    int firmware_probe_pending;
    unsigned int firmware_probe_ticks;
    unsigned int firmware_probe_attempts;
} deneb_print_service_t;

void deneb_print_service_init(deneb_print_service_t *svc);
int deneb_print_service_open_motion(deneb_print_service_t *svc);
int deneb_print_service_handle_command(deneb_print_service_t *svc,
                                       const deneb_command_t *cmd,
                                       char *reply, size_t reply_sz);
int deneb_print_service_poll_motion(deneb_print_service_t *svc);
int deneb_print_service_poll_job(deneb_print_service_t *svc);
void deneb_print_service_refresh_diagnostics(deneb_print_service_t *svc);
void deneb_print_service_close(deneb_print_service_t *svc);

#endif
