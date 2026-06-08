/* SPDX-License-Identifier: MPL-2.0 */
#include "service_command.h"
#include "service_context.h"
#include "service.h"

void deneb_print_service_init(deneb_print_service_t *svc)
{
    deneb_service_context_init(svc);
}

int deneb_print_service_open_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (deneb_service_context_motion_runtime(svc, &runtime) < 0)
        return -1;

    return deneb_motion_runtime_open(&runtime);
}

int deneb_print_service_poll_motion(deneb_print_service_t *svc)
{
    deneb_motion_runtime_t runtime;

    if (deneb_service_context_motion_runtime(svc, &runtime) < 0)
        return -1;

    return deneb_motion_runtime_poll(&runtime);
}

void deneb_print_service_refresh_diagnostics(deneb_print_service_t *svc)
{
    deneb_service_context_refresh_diagnostics(svc);
}

int deneb_print_service_handle_command(deneb_print_service_t *svc,
                                       const deneb_command_t *cmd,
                                       char *reply, size_t reply_sz)
{
    return deneb_service_command_handle(svc, cmd, reply, reply_sz);
}

int deneb_print_service_poll_job(deneb_print_service_t *svc)
{
    deneb_job_streamer_t streamer;

    if (deneb_service_context_job_streamer(svc, &streamer) < 0)
        return -1;

    return deneb_job_streamer_poll(&streamer);
}

void deneb_print_service_close(deneb_print_service_t *svc)
{
    deneb_service_context_close(svc);
}
