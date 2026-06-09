/* SPDX-License-Identifier: MPL-2.0 */
#include "heater_wait.h"
#include "print_control.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>

void deneb_heater_wait_init(deneb_heater_wait_t *wait)
{
    memset(wait, 0, sizeof(*wait));
    wait->tolerance = 1.0f;
}

void deneb_heater_wait_start(deneb_heater_wait_t *wait, float bed_target,
                             float head_target, float tolerance)
{
    if (!wait)
        return;

    wait->bed_target = bed_target;
    wait->head_target = head_target;
    wait->tolerance = tolerance > 0.0f ? tolerance : 1.0f;
    wait->wait_bed = bed_target > 0.0f;
    wait->wait_head = head_target > 0.0f;
    wait->active = (bed_target > 0.0f || head_target > 0.0f);
}

void deneb_heater_wait_start_bed(deneb_heater_wait_t *wait, float bed_target,
                                 float tolerance)
{
    if (!wait)
        return;
    wait->bed_target = bed_target;
    wait->head_target = 0.0f;
    wait->tolerance = tolerance > 0.0f ? tolerance : 1.0f;
    wait->wait_bed = bed_target > 0.0f;
    wait->wait_head = 0;
    wait->active = wait->wait_bed;
}

void deneb_heater_wait_start_head(deneb_heater_wait_t *wait, float head_target,
                                  float tolerance)
{
    if (!wait)
        return;
    wait->bed_target = 0.0f;
    wait->head_target = head_target;
    wait->tolerance = tolerance > 0.0f ? tolerance : 1.0f;
    wait->wait_bed = 0;
    wait->wait_head = head_target > 0.0f;
    wait->active = wait->wait_head;
}

int deneb_heater_wait_ready(const deneb_heater_wait_t *wait,
                            const deneb_status_t *status)
{
    if (!wait || !status || !wait->active)
        return 1;

    return (!wait->wait_bed ||
            deneb_print_temp_target_ready(status->bed_t_cur, wait->bed_target,
                                          wait->tolerance)) &&
           (!wait->wait_head ||
            deneb_print_temp_target_ready(status->head_t_cur, wait->head_target,
                                          wait->tolerance));
}

void deneb_heater_wait_apply_status(const deneb_heater_wait_t *wait,
                                    deneb_status_t *status)
{
    if (!wait || !status || !wait->active)
        return;

    status->state = DENEB_PRINT_STATE_PREPARING;
    if (wait->wait_bed)
        status->bed_t_set = wait->bed_target;
    if (wait->wait_head)
        status->head_t_set = wait->head_target;
    if (!status->req[0])
        snprintf(status->req, sizeof(status->req), "%s",
                 deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PREPARING));
}
