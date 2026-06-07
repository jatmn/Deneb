/* SPDX-License-Identifier: MPL-2.0 */
#include "heater_wait.h"

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
    wait->active = (bed_target > 0.0f || head_target > 0.0f);
}

static int target_ready(float current, float target, float tolerance)
{
    if (target <= 0.0f)
        return 1;
    return current >= target - tolerance;
}

int deneb_heater_wait_ready(const deneb_heater_wait_t *wait,
                            const deneb_status_t *status)
{
    if (!wait || !status || !wait->active)
        return 1;

    return target_ready(status->bed_t_cur, wait->bed_target, wait->tolerance) &&
           target_ready(status->head_t_cur, wait->head_target, wait->tolerance);
}

void deneb_heater_wait_apply_status(const deneb_heater_wait_t *wait,
                                    deneb_status_t *status)
{
    if (!wait || !status || !wait->active)
        return;

    status->state = DENEB_PRINT_STATE_PREPARING;
    status->bed_t_set = wait->bed_target;
    status->head_t_set = wait->head_target;
    if (!status->req[0])
        snprintf(status->req, sizeof(status->req), "PREHEAT");
}
