/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_PAUSE_RESUME_H
#define DENEB_PRINTSVC_PAUSE_RESUME_H

#include "status.h"

int deneb_pause_resume_pause(deneb_status_t *status);
int deneb_pause_resume_resume(deneb_status_t *status, int heater_wait_active);

#endif
