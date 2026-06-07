/* SPDX-License-Identifier: MPL-2.0 */
#include "ipc_zmq.h"
#include "config.h"
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

int main(int argc, char **argv)
{
    deneb_print_service_t svc;
    int allow_programming = 0;

    if (argc > 1 && strcmp(argv[1], "--smoke-test") == 0)
        return smoke_test();
    if (argc > 1 && strcmp(argv[1], "--program-motion-firmware") == 0)
        allow_programming = 1;

    deneb_print_service_init(&svc);
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
