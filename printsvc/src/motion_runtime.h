/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_MOTION_RUNTIME_H
#define DENEB_PRINTSVC_MOTION_RUNTIME_H

#include "flow_control.h"
#include "heater_wait.h"
#include "serial_transport.h"
#include "status.h"

typedef struct {
    deneb_status_t *status;
    deneb_heater_wait_t *heater_wait;
    deneb_flow_control_t *flow;
    deneb_serial_transport_t *serial;
    int *serial_ready;
} deneb_motion_runtime_t;

int deneb_motion_runtime_open(deneb_motion_runtime_t *runtime);
int deneb_motion_runtime_poll(deneb_motion_runtime_t *runtime);
void deneb_motion_runtime_close(deneb_motion_runtime_t *runtime);

#endif
