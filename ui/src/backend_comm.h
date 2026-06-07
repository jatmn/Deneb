/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend communication module for Deneb UI.
 * Connects to the stock coordinator via ZeroMQ.
 *
 * Two sockets:
 *   1. SUB on tcp://127.0.0.1:5565 - receives printer status (topic "10001")
 *   2. REQ on tcp://127.0.0.1:5566 - sends commands (GCODE, JOB, PAUSE, etc.)
 *
 * Status updates are polled in the LVGL main loop and cached locally.
 * UI screens read the cached state directly (no locking needed on single thread).
 */

#ifndef BACKEND_COMM_H
#define BACKEND_COMM_H

#include <stdbool.h>
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
    int time_left;       /* seconds */
    float progress;      /* 0-100, calculated from time */

    /* State */
    bool is_printing;
    bool is_paused;
    bool has_error;
    char current_req[32]; /* GCODE, JOB, PAUSE, etc. */

    /* Connection */
    bool connected;
    uint32_t last_update_ms; /* timestamp of last status received */
} printer_state_t;

/**
 * Initialize backend communication.
 * Creates ZMQ SUB and REQ sockets.
 * Returns 0 on success, -1 on failure.
 */
int backend_init(void);

/**
 * Poll for new status updates from the coordinator.
 * Call this once per main loop iteration (~every 5-20ms).
 * Non-blocking -- returns immediately if no data available.
 */
void backend_poll(void);

/**
 * Get the current cached printer state.
 * Safe to call from any screen at any time.
 */
const printer_state_t *backend_get_state(void);

/**
 * Send a G-code command to the printer.
 * Example: backend_send_gcode("M140 S60")
 * Returns 0 on success, -1 on send failure.
 */
int backend_send_gcode(const char *gcode);

/**
 * Send a command to the coordinator.
 * Example: backend_send_command("PAUSE", "{}")
 * Returns 0 on success, -1 on send failure.
 */
int backend_send_command(const char *cmd, const char *args_json);

/**
 * Abort the current print.
 */
int backend_abort_print(void);

/**
 * Force-stop the current print, cool down nozzle/bed, and home axes.
 */
int backend_stop_print(void);

/**
 * Whether a stop request is currently in-flight.
 */
int backend_is_stop_print_inflight(void);

/**
 * Pause the current print.
 */
int backend_pause_print(void);

/**
 * Resume the current print.
 */
int backend_resume_print(void);

/**
 * Cleanup: close ZMQ sockets and context.
 */
void backend_deinit(void);

#endif /* BACKEND_COMM_H */
