/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend ZMQ communication for deneb-api.
 * Stock coordinator remains the default route; native deneb-printsvc is used
 * when the lab gate is enabled.
 */

#ifndef BACKEND_ZMQ_H
#define BACKEND_ZMQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    int time_total;      /* seconds */
    int time_left;       /* seconds (from ZMQ field "Tleft") */
    float progress;      /* 0-100 */

    /* State */
    bool is_printing;
    bool is_paused;
    bool has_error;
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

/* Send a command to the selected backend. Returns 0 on success. */
int backend_zmq_send_command(const char *cmd, const char *args);

/* Send raw G-code. Returns 0 on success. */
int backend_zmq_send_gcode(const char *gcode);
int backend_zmq_send_gcodes(const char *const *gcodes, size_t count);
int backend_zmq_send_macro(const char *macro);
int backend_zmq_send_job(const char *path, const char *source,
                         const char *uuid, float bed_target,
                         float head_target);

/* Convenience: pause/resume/abort print. */
int backend_zmq_pause(void);
int backend_zmq_resume(void);
int backend_zmq_abort(void);
int backend_zmq_stop_print(void);

/* Get pre-serialized status JSON string. */
const char *backend_zmq_get_status_json(void);
const char *backend_zmq_get_print_backend_name(void);
const char *backend_zmq_get_print_backend_status_url(void);
const char *backend_zmq_get_print_backend_command_url(void);

/* Cleanup. */
void backend_zmq_deinit(void);

#endif /* BACKEND_ZMQ_H */
