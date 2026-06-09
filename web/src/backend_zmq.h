/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend ZMQ communication for deneb-api.
 * Uses native deneb-printsvc for print status and commands.
 */

#ifndef BACKEND_ZMQ_H
#define BACKEND_ZMQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "print_backend_route.h"
#include "print_job_summary.h"
#include "printer_status_response.h"

/* Printer state, updated from ZMQ SUB status stream */
typedef struct {
    /* Temperatures */
    float nozzle_temp_cur;
    float nozzle_temp_set;
    float bed_temp_cur;
    float bed_temp_set;
    float topcap_temp_cur;
    bool topcap_present;

    /* Position */
    float pos_x;
    float pos_y;
    float pos_z;
    float pos_e;

    /* Print job */
    char filename[128];
    char source[32];
    char uuid[64];
    char firmware[64];
    char machine_type[16];
    int pcb_id;
    bool pcb_id_valid;
    int time_total;      /* seconds */
    int time_left;       /* seconds (from ZMQ field "Tleft") */
    float progress;      /* 0-100 */

    /* State */
    bool is_printing;
    bool is_paused;
    bool has_error;
    bool native_active;
    bool native_stop_allowed;
    bool has_native_active;
    bool has_native_stop_allowed;
    char current_req[32];

    /* Connection */
    bool connected;
    uint32_t last_update_ms;
} printer_state_t;

/* Initialize ZMQ connections. Returns 0 on success. */
int backend_zmq_init(void);

/* Get the ZMQ SUB socket fd for epoll registration. Returns -1 if not available. */
int backend_zmq_get_fd(void);

/* Poll for new status data. Call when epoll signals data available. */
void backend_zmq_poll(void);

/* Get cached printer state. */
const printer_state_t *backend_zmq_get_state(void);
const char *backend_zmq_get_status_label(void);
void backend_zmq_get_job_summary(deneb_print_job_summary_t *summary);
void backend_zmq_get_printer_status_response(
    deneb_printer_status_response_t *status);
int backend_zmq_has_active_job(void);
int backend_zmq_print_start_allowed(void);
int backend_zmq_manual_action_allowed(void);

/* Send a command to the native print service. Returns 0 on success. */
int backend_zmq_send_command(const char *cmd, const char *args);

/* Send raw G-code. Returns 0 on success. */
int backend_zmq_send_gcode(const char *gcode);
int backend_zmq_send_gcodes(const char *const *gcodes, size_t count);
int backend_zmq_send_macro(const char *macro);
int backend_zmq_send_job(const char *path, const char *source,
                         const char *uuid, float bed_target,
                         float head_target);
int backend_zmq_send_pending_instruction(const char *instruction);

/* Convenience: pause/resume/abort print. */
int backend_zmq_pause(void);
int backend_zmq_resume(void);
int backend_zmq_abort(void);
int backend_zmq_stop_print(void);

/* Get pre-serialized status JSON string. */
const char *backend_zmq_get_status_json(void);
deneb_print_backend_t backend_zmq_get_print_backend(void);
const char *backend_zmq_get_print_backend_name(void);
const char *backend_zmq_get_print_backend_status_url(void);
const char *backend_zmq_get_print_backend_command_url(void);

/* Cleanup. */
void backend_zmq_deinit(void);

#endif /* BACKEND_ZMQ_H */
