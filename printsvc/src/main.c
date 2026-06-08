/* SPDX-License-Identifier: MPL-2.0 */
#include "ipc_zmq.h"
#include "config.h"
#include "diagnostics_log.h"
#include "motion_firmware.h"
#include "service.h"

#include <stdio.h>
#include <string.h>

static int smoke_test(void)
{
    deneb_print_service_t svc;
    char frame[1024];

    deneb_print_service_init(&svc);
    if (deneb_status_serialize_frame(&svc.status, frame, sizeof(frame)) < 0)
        return 1;
    if (strncmp(frame, "10001<", 6) != 0)
        return 1;
    return 0;
}

static int local_job_smoke(const char *path)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[640];
    char reply[128];
    int rc = 1;

    if (!path || !*path)
        return 1;
    if (strchr(path, '"') || strchr(path, '\\'))
        return 1;

    if (snprintf(frame, sizeof(frame),
                 "JOB<{\"file\":\"%s\",\"source\":\"USB\","
                 "\"uuid\":\"deneb-local-smoke\"}",
                 path) >= (int)sizeof(frame))
        return 1;

    deneb_print_service_init(&svc);
    if (deneb_command_parse(frame, &cmd) != 0)
        goto out;
    if (deneb_print_service_handle_command(&svc, &cmd, reply,
                                           sizeof(reply)) != 0)
        goto out;
    if (!svc.job_active)
        goto out;
    if (strcmp(svc.status.file, path) != 0)
        goto out;
    if (strcmp(svc.status.source, "USB") != 0)
        goto out;

    if (deneb_command_parse("ABORT<{}", &cmd) != 0)
        goto out;
    if (deneb_print_service_handle_command(&svc, &cmd, reply,
                                           sizeof(reply)) != 0)
        goto out;
    if (svc.job_active)
        goto out;
    if (strcmp(svc.status.file, "none") != 0)
        goto out;

    rc = 0;
out:
    deneb_print_service_close(&svc);
    return rc;
}

int main(int argc, char **argv)
{
    deneb_print_service_t svc;
    int allow_programming = 0;

    if (argc > 1 && strcmp(argv[1], "--smoke-test") == 0)
        return smoke_test();
    if (argc > 2 && strcmp(argv[1], "--local-job-smoke") == 0)
        return local_job_smoke(argv[2]);
    if (argc > 1 && strcmp(argv[1], "--program-motion-firmware") == 0)
        allow_programming = 1;

    deneb_print_service_init(&svc);
    deneb_diagnostics_log_open(NULL);
    deneb_motion_fw_result_t fw = deneb_motion_firmware_ensure(
        DENEB_MOTION_FW_HEX, DENEB_MOTION_FW_CACHE, DENEB_MOTION_FW_PROGRAMMER,
        allow_programming);
    if (fw == DENEB_MOTION_FW_ERROR) {
        fprintf(stderr, "deneb-printsvc: motion firmware verification unavailable\n");
    } else if (fw == DENEB_MOTION_FW_PROGRAM_REQUIRED) {
        fprintf(stderr, "deneb-printsvc: motion firmware programming required but not enabled\n");
    }
    if (deneb_print_service_open_motion(&svc) != 0) {
        fprintf(stderr, "deneb-printsvc: motion serial unavailable; running without serial writes\n");
    }

    return deneb_printsvc_ipc_run(&svc);
}
