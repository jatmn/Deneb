/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend communication module for Deneb UI.
 * Connects to native deneb-printsvc via ZeroMQ.
 *
 * Two sockets:
 *   1. SUB receives printer status on topic "10001"
 *   2. REQ sends commands (GCODE, JOB, PAUSE, etc.)
 *
 * Status updates are polled in the LVGL main loop and cached locally.
 * UI screens read the cached state directly (no locking needed on single thread).
 */

#ifndef BACKEND_COMM_H
#define BACKEND_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "print_backend_route.h"
#include "status_state.h"

/* Printer state, updated from ZMQ SUB status stream */
typedef deneb_backend_status_state_t printer_state_t;

/**
 * Initialize backend communication.
 * Creates ZMQ SUB and REQ sockets.
 * Returns 0 on success, -1 on failure.
 */
int backend_init(void);

/**
 * Poll for new native print-service status updates.
 * Call this once per main loop iteration (~every 5-20ms).
 * Non-blocking -- returns immediately if no data available.
 */
void backend_poll(void);

/**
 * Get the current cached printer state.
 * Safe to call from any screen at any time.
 */
const printer_state_t *backend_get_state(void);
deneb_print_backend_t backend_get_print_backend(void);
const char *backend_get_print_backend_name(void);
const char *backend_get_print_backend_status_url(void);
const char *backend_get_print_backend_command_url(void);
int backend_is_ready(void);
int backend_print_start_allowed(void);
int backend_manual_action_allowed(void);
int backend_get_print_display_name(char *out, size_t out_sz);
int backend_has_print_name(const char *display_name);
int backend_has_active_print_context(void);
int backend_has_preparing_print_context(void);
int backend_has_stoppable_print_context(void);
int backend_has_abort_print_context(void);
void backend_clear_print_display_context_if_idle(void);

/**
 * Send a G-code command to the printer.
 * Example: backend_send_gcode("M140 S60")
 * Returns 0 on success, -1 on send failure.
 */
int backend_send_gcode(const char *gcode);
int backend_send_gcodes(const char *const *gcodes, size_t count);
int backend_send_macro(const char *macro);
int backend_send_job(const char *path, const char *source, const char *uuid,
                     float bed_target, float head_target);
int backend_send_pending_instruction(const char *instruction);

/**
 * Send a command to the native print service.
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
