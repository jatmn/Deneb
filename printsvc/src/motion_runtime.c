/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_runtime.h"

#include "config.h"
#include "error_map.h"
#include "motion_observer.h"
#include "motion_sender.h"

static int runtime_valid(const deneb_motion_runtime_t *runtime)
{
    return runtime && runtime->status && runtime->heater_wait &&
           runtime->flow && runtime->serial && runtime->serial_ready;
}

static int runtime_sequence_fault_is_fatal(const deneb_motion_runtime_t *runtime)
{
    return deneb_status_has_active_print(runtime->status);
}

int deneb_motion_runtime_open(deneb_motion_runtime_t *runtime)
{
    if (!runtime_valid(runtime))
        return -1;

    if (deneb_serial_open(runtime->serial, DENEB_PRINTSVC_SERIAL_DEVICE,
                          DENEB_PRINTSVC_SERIAL_BAUD) == 0) {
        *runtime->serial_ready = 1;
        return 0;
    }

    *runtime->serial_ready = 0;
    return -1;
}

int deneb_motion_runtime_poll(deneb_motion_runtime_t *runtime)
{
    char line[DENEB_PRINTSVC_SERIAL_LINE];
    uint8_t resend_sequence = 0;
    int handled = 0;

    if (!runtime_valid(runtime))
        return -1;
    if (!*runtime->serial_ready)
        return 0;

    for (;;) {
        int n = deneb_serial_read_line(runtime->serial, line, sizeof(line));
        if (n < 0)
            return -1;
        if (n == 0)
            break;

        handled++;
        int flow_rc = deneb_motion_observer_handle_line(
            runtime->status, runtime->heater_wait, runtime->flow, line,
            &resend_sequence);
        if (flow_rc == 2) {
            (void)resend_sequence;
            if (deneb_motion_sender_resend_pending(runtime->flow,
                                                   runtime->serial,
                                                   *runtime->serial_ready) != 0) {
                if (!runtime_sequence_fault_is_fatal(runtime))
                    continue;
                runtime->status->state = DENEB_PRINT_STATE_ERROR;
                runtime->status->error = deneb_error_make(DENEB_ERROR_SERIAL,
                                                          "resend failed");
                return -1;
            }
        } else if (flow_rc == 3) {
            deneb_flow_resync_to_expected(runtime->flow);
            if (runtime_sequence_fault_is_fatal(runtime) &&
                !runtime->allow_sequence_resync) {
                runtime->status->state = DENEB_PRINT_STATE_ERROR;
                runtime->status->error = deneb_error_make(
                    DENEB_ERROR_SERIAL, "flow control sequence desync");
                return -1;
            }
        } else if (flow_rc < 0 && runtime_sequence_fault_is_fatal(runtime)) {
            runtime->status->state = DENEB_PRINT_STATE_ERROR;
            runtime->status->error = deneb_error_make(
                DENEB_ERROR_SERIAL, "flow control rejected response");
            return -1;
        }
    }

    return handled;
}

void deneb_motion_runtime_close(deneb_motion_runtime_t *runtime)
{
    if (!runtime_valid(runtime))
        return;

    deneb_serial_close(runtime->serial);
    *runtime->serial_ready = 0;
}
