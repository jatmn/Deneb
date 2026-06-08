/* SPDX-License-Identifier: MPL-2.0 */
#include "pause_resume_control.h"

#include "command_reply.h"
#include "pause_resume.h"

int deneb_pause_resume_control_pause(deneb_print_service_t *svc,
                                     char *reply, size_t reply_sz)
{
    if (!svc || !reply || reply_sz == 0)
        return -1;

    if (deneb_pause_resume_pause(&svc->status) < 0) {
        deneb_command_reply_error(reply, reply_sz, "no active print to pause");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "pause accepted");
    return 0;
}

int deneb_pause_resume_control_resume(deneb_print_service_t *svc,
                                      char *reply, size_t reply_sz)
{
    if (!svc || !reply || reply_sz == 0)
        return -1;

    if (deneb_pause_resume_resume(&svc->status,
                                  svc->heater_wait.active) < 0) {
        deneb_command_reply_error(reply, reply_sz, "print is not paused");
        return -1;
    }

    deneb_command_reply_ok(reply, reply_sz, "resume accepted");
    return 0;
}
