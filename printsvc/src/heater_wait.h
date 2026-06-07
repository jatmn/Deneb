/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_HEATER_WAIT_H
#define DENEB_PRINTSVC_HEATER_WAIT_H

#include "status.h"

typedef struct {
    float bed_target;
    float head_target;
    float tolerance;
    int active;
} deneb_heater_wait_t;

void deneb_heater_wait_init(deneb_heater_wait_t *wait);
void deneb_heater_wait_start(deneb_heater_wait_t *wait, float bed_target,
                             float head_target, float tolerance);
int deneb_heater_wait_ready(const deneb_heater_wait_t *wait,
                            const deneb_status_t *status);
void deneb_heater_wait_apply_status(const deneb_heater_wait_t *wait,
                                    deneb_status_t *status);

#endif
