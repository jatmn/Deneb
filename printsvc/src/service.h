/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SERVICE_H
#define DENEB_PRINTSVC_SERVICE_H

#include "command.h"
#include "flow_control.h"
#include "gcode_stream.h"
#include "heater_wait.h"
#include "serial_transport.h"
#include "status.h"

typedef struct {
    deneb_status_t status;
    deneb_serial_transport_t serial;
    deneb_flow_control_t flow;
    deneb_gcode_stream_t job_stream;
    deneb_heater_wait_t heater_wait;
    int serial_ready;
    int job_active;
    int abort_requested;
    unsigned int planner_starvation_count;
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
