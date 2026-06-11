/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend communication implementation.
 * Connects to native deneb-printsvc.
 *
 * Build note: requires libzmq (static link for MIPS target).
 * For host testing without libzmq, define BACKEND_COMM_STUB to
 * compile a stub that returns dummy data.
 */

#include "backend_comm.h"
#include "command_format.h"
#include "json_field.h"
#include "pending_job_dispatch.h"
#include "pending_job_file.h"
#include "print_backend_route.h"
#include "print_job_file.h"
#include "print_state_rules.h"
#include "status_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef BACKEND_COMM_STUB

/* ========== STUB IMPLEMENTATION (for host testing without ZMQ) ========== */

static printer_state_t state = {
    .nozzle_temp_cur = 0, .nozzle_temp_set = 0,
    .bed_temp_cur = 0, .bed_temp_set = 0,
    .pos_x = 0, .pos_y = 0, .pos_z = 0, .pos_e = 0,
    .filename = "(stub)", .source = "usb", .uuid = "",
    .time_total = 0, .time_left = 0, .progress = 0,
    .is_printing = false, .is_paused = false, .has_error = false,
    .native_active = false, .native_stop_allowed = false,
    .has_native_active = false, .has_native_stop_allowed = false,
    .current_req = "", .connected = false, .last_update_ms = 0,
};
static deneb_print_backend_route_t backend_route = {
    DENEB_PRINT_BACKEND_NATIVE,
    DENEB_PRINTSVC_STATUS_URL,
    DENEB_PRINTSVC_COMMAND_URL
};

int backend_init(void) {
    fprintf(stderr, "backend: stub mode (no ZMQ)\n");
    state.connected = true;
    return 0;
}

void backend_poll(void) { /* no-op in stub */ }
const printer_state_t *backend_get_state(void) { return &state; }
int backend_get_print_display_name(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';
    return deneb_pending_job_file_display_value(state.filename, out, out_sz);
}
int backend_has_print_name(const char *display_name)
{
    return deneb_print_file_is_candidate(display_name) ||
           deneb_print_file_is_candidate(state.filename);
}
int backend_has_active_print_context(void) { return 0; }
int backend_has_preparing_print_context(void) { return 0; }
int backend_has_stoppable_print_context(void) { return 0; }
int backend_has_abort_print_context(void) { return 0; }
void backend_clear_print_display_context_if_idle(void) {}
deneb_print_backend_t backend_get_print_backend(void) { return backend_route.backend; }
const char *backend_get_print_backend_name(void) { return deneb_print_backend_name(backend_route.backend); }
const char *backend_get_print_backend_status_url(void) { return backend_route.status_url; }
const char *backend_get_print_backend_command_url(void) { return backend_route.command_url; }
int backend_is_ready(void) { return state.connected && !state.has_error; }
int backend_print_start_allowed(void)
{
    return deneb_print_start_allowed(state.connected, state.has_error,
                                     state.is_paused, state.is_printing);
}
int backend_manual_action_allowed(void)
{
    return deneb_print_manual_action_allowed(state.connected, state.has_error,
                                             state.is_paused,
                                             state.is_printing);
}
int backend_is_stop_print_inflight(void) { return 0; }
int backend_send_gcode(const char *gcode) { (void)gcode; return 0; }
int backend_send_gcodes(const char *const *gcodes, size_t count) { (void)gcodes; (void)count; return 0; }
int backend_send_macro(const char *macro) { (void)macro; return 0; }
int backend_send_job(const char *path, const char *source, const char *uuid,
                     float bed_target, float head_target)
{
    (void)path; (void)source; (void)uuid; (void)bed_target; (void)head_target;
    return 0;
}
int backend_send_pending_instruction(const char *instruction) { (void)instruction; return 0; }
int backend_send_command(const char *cmd, const char *args) { (void)cmd; (void)args; return 0; }
int backend_abort_print(void) { return 0; }
int backend_stop_print(void) { return 0; }
int backend_pause_print(void) { return 0; }
int backend_resume_print(void) { return 0; }
void backend_deinit(void) { /* no-op */ }

#else /* ========== REAL ZMQ IMPLEMENTATION ========== */

#include <zmq.h>
#include <errno.h>

#define STATUS_TOPIC "10001"
#define MAX_STATUS_MSGS_PER_POLL 4
#define STOP_INFLIGHT_MS 3000
#define STATUS_QUEUE_HWM 4

static deneb_print_backend_route_t backend_route;

static void *zmq_ctx = NULL;
static void *status_socket = NULL;  /* SUB - native print status */
static void *rpc_socket = NULL;     /* REQ - native print commands */

static printer_state_t state = {0};
static int had_previous_status = 0;
static printer_state_t previous_state = {0};
static deneb_print_preheat_tracker_t preheat_tracker;
static char retained_print_filename[128];
static deneb_print_stop_guard_t stop_guard;

static int configure_socket_linger(void *socket)
{
    int linger = 0;
    return zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
}

static void configure_status_socket(void *socket)
{
    int hwm = STATUS_QUEUE_HWM;
    int conflate = 1;

    if (!socket)
        return;

    zmq_setsockopt(socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(socket, ZMQ_CONFLATE, &conflate, sizeof(conflate));
}

static void close_rpc_socket(void)
{
    if (rpc_socket) {
        zmq_close(rpc_socket);
        rpc_socket = NULL;
    }
}

static int open_rpc_socket(void)
{
    if (!zmq_ctx)
        return -1;

    close_rpc_socket();

    rpc_socket = zmq_socket(zmq_ctx, ZMQ_REQ);
    if (!rpc_socket) {
        fprintf(stderr, "backend: rpc socket failed\n");
        return -1;
    }

    configure_socket_linger(rpc_socket);

    if (zmq_connect(rpc_socket, backend_route.command_url) != 0) {
        fprintf(stderr, "backend: connect %s failed: %s\n",
                backend_route.command_url, zmq_strerror(errno));
        close_rpc_socket();
        return -1;
    }

    return 0;
}

static void set_filename_or_none(char *dst, const char *value)
{
    if (deneb_pending_job_file_display_value(value, dst, 128) != 0)
        dst[0] = '\0';
}

static void retain_print_filename(const char *filename)
{
    deneb_pending_job_file_display_value(filename, retained_print_filename,
                                         sizeof(retained_print_filename));
}

static void log_status_transition(const printer_state_t *curr)
{
    deneb_status_transition_t transition;
    int preheat_events;

    if (!had_previous_status) {
        previous_state = *curr;
        had_previous_status = 1;
        return;
    }

    if (deneb_status_state_transition_from_pair(
            &transition, &previous_state, curr) != 0)
        return;

    if (transition.req_changed)
        fprintf(stderr, "backend: printer req changed: \"%s\" -> \"%s\"\n",
                previous_state.current_req[0] ? previous_state.current_req : "none",
                curr->current_req[0] ? curr->current_req : "none");

    if (transition.print_resumed)
        fprintf(stderr, "backend: print resumed (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");
    if (transition.print_paused)
        fprintf(stderr, "backend: print paused (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");

    if (transition.print_ended) {
        if (strcmp(transition.completion_label, "error") == 0)
            fprintf(stderr, "backend: print ended with error (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else if (strcmp(transition.completion_label, "completed") == 0)
            fprintf(stderr, "backend: print completed (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else
            fprintf(stderr, "backend: print stopped before completion (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        retained_print_filename[0] = '\0';
    }

    if (transition.print_started)
        fprintf(stderr, "backend: print started (filename=%s, req=%s, uuid=%s, source=%s)\n",
                curr->filename[0] ? curr->filename : "(unknown)",
                curr->current_req[0] ? curr->current_req : "unknown",
                curr->uuid[0] ? curr->uuid : "(none)",
                curr->source[0] ? curr->source : "(none)");

    preheat_events =
        deneb_status_state_preheat_events(curr, &preheat_tracker);

    if (preheat_events & DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE) {
        fprintf(stderr, "backend: print preheating targets active: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_set, curr->nozzle_temp_set);
    }

    if (preheat_events & DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY) {
        fprintf(stderr, "backend: print preheat targets reached: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_cur, curr->nozzle_temp_cur);
    }

    previous_state = *curr;
}

/**
 * Parse a status JSON payload and update the cached state.
 */
static void parse_status(const char *json)
{
    printer_state_t prev = state;
    struct timespec ts;
    uint32_t now_ms;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    if (deneb_status_state_apply_json(&state, &prev, json,
                                      retained_print_filename,
                                      sizeof(retained_print_filename),
                                      &stop_guard, now_ms) != 0)
        return;

    log_status_transition(&state);
}

int backend_init(void)
{
    memset(&state, 0, sizeof(state));
    deneb_print_stop_guard_init(&stop_guard, STOP_INFLIGHT_MS);
    deneb_print_preheat_tracker_init(&preheat_tracker);
    backend_route = deneb_print_backend_route_detect();

    zmq_ctx = zmq_ctx_new();
    if (!zmq_ctx) {
        fprintf(stderr, "backend: zmq_ctx_new failed\n");
        return -1;
    }

    /* SUB socket for status updates */
    status_socket = zmq_socket(zmq_ctx, ZMQ_SUB);
    if (!status_socket) {
        fprintf(stderr, "backend: status socket failed\n");
        return -1;
    }
    configure_status_socket(status_socket);
    if (zmq_connect(status_socket, backend_route.status_url) != 0) {
        fprintf(stderr, "backend: connect %s failed: %s\n",
                backend_route.status_url, zmq_strerror(errno));
        return -1;
    }
    zmq_setsockopt(status_socket, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC));

    /* REQ socket for commands */
    if (open_rpc_socket() != 0)
        return -1;

    /* Set linger to 0 so we don't block on shutdown */
    configure_socket_linger(status_socket);

    fprintf(stderr, "backend: selected %s print backend, connected to %s (status) and %s (rpc)\n",
            deneb_print_backend_name(backend_route.backend),
            backend_route.status_url, backend_route.command_url);

    return 0;
}

void backend_poll(void)
{
    if (!status_socket)
        return;

    zmq_msg_t msg;
    int msg_count = 0;

    /* Non-blocking receive on status socket */
    while (msg_count < MAX_STATUS_MSGS_PER_POLL) {
        zmq_msg_init(&msg);
        int rc = zmq_msg_recv(&msg, status_socket, ZMQ_DONTWAIT);
        if (rc < 0) {
            zmq_msg_close(&msg);
            break;
        }
        msg_count++;

        /* Parse: expect "10001<{json}" */
        const char *data = (const char *)zmq_msg_data(&msg);
        size_t len = zmq_msg_size(&msg);

        if (len > 6 && memcmp(data, "10001<", 6) == 0) {
            /* Extract JSON (skip "10001<" prefix, strip trailing "}" if needed) */
            size_t json_len = len - 6;
            char *json_buf = malloc(json_len + 1);
            if (json_buf) {
                memcpy(json_buf, data + 6, json_len);
                json_buf[json_len] = '\0';
                parse_status(json_buf);
                free(json_buf);
            }
        }

        zmq_msg_close(&msg);
    }
}

const printer_state_t *backend_get_state(void)
{
    return &state;
}

int backend_get_print_display_name(char *out, size_t out_sz)
{
    deneb_status_filename_context_t curr;
    deneb_status_filename_context_t prev;
    const printer_state_t *prev_state;

    if (!out || out_sz == 0)
        return -1;

    out[0] = '\0';
    prev_state = had_previous_status ? &previous_state : &state;
    curr = deneb_status_state_filename_context(&state);
    prev = deneb_status_state_filename_context(prev_state);

    deneb_status_payload_resolve_filename_value(
        state.filename,
        state.filename[0] && strcmp(state.filename, DENEB_PRINT_NONE_VALUE) != 0,
        &curr, &prev, retained_print_filename,
        sizeof(retained_print_filename), out, out_sz);
    return out[0] ? 0 : -1;
}

int backend_has_print_name(const char *display_name)
{
    return deneb_status_state_has_print_name(&state, display_name);
}

int backend_has_active_print_context(void)
{
    char display_name[128];
    int has_print_name;
    deneb_print_context_flags_t flags;

    if (state.has_native_active)
        return state.native_active;

    backend_get_print_display_name(display_name, sizeof(display_name));
    has_print_name = deneb_status_state_has_print_name(&state, display_name);
    flags = deneb_status_state_context_flags(&state, has_print_name);
    return flags.has_active_context;
}

int backend_has_preparing_print_context(void)
{
    char display_name[128];
    int has_print_name;
    deneb_print_context_flags_t flags;

    backend_get_print_display_name(display_name, sizeof(display_name));
    has_print_name = deneb_status_state_has_print_name(&state, display_name);
    flags = deneb_status_state_context_flags(&state, has_print_name);
    return flags.has_preparing_context;
}

int backend_has_stoppable_print_context(void)
{
    char display_name[128];
    int has_print_name;
    deneb_print_context_flags_t flags;

    if (state.has_native_stop_allowed)
        return state.native_stop_allowed;

    backend_get_print_display_name(display_name, sizeof(display_name));
    has_print_name = deneb_status_state_has_print_name(&state, display_name);
    flags = deneb_status_state_context_flags(&state, has_print_name);
    return flags.has_stoppable_context;
}

int backend_has_abort_print_context(void)
{
    return deneb_status_state_has_abort_context(
        &state, backend_is_stop_print_inflight());
}

void backend_clear_print_display_context_if_idle(void)
{
    char display_name[128];
    int has_print_name;

    backend_get_print_display_name(display_name, sizeof(display_name));
    has_print_name = deneb_status_state_has_print_name(&state, display_name);

    if (!backend_has_active_print_context() &&
        !has_print_name &&
        state.time_total <= 0 &&
        state.time_left <= 0 &&
        state.bed_temp_set <= 0.0f &&
        state.nozzle_temp_set <= 0.0f) {
        retained_print_filename[0] = '\0';
    }
}

deneb_print_backend_t backend_get_print_backend(void)
{
    return backend_route.backend;
}

const char *backend_get_print_backend_name(void)
{
    return deneb_print_backend_name(backend_route.backend);
}

const char *backend_get_print_backend_status_url(void)
{
    return backend_route.status_url;
}

const char *backend_get_print_backend_command_url(void)
{
    return backend_route.command_url;
}

int backend_is_ready(void)
{
    return state.connected && !state.has_error;
}

int backend_print_start_allowed(void)
{
    return deneb_print_start_allowed(state.connected, state.has_error,
                                     state.is_paused, state.is_printing);
}

int backend_manual_action_allowed(void)
{
    return deneb_print_manual_action_allowed(state.connected, state.has_error,
                                             state.is_paused,
                                             state.is_printing);
}

int backend_is_stop_print_inflight(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return stop_guard.in_flight;

    return deneb_print_stop_guard_inflight(
        &stop_guard,
        (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL,
        deneb_status_state_has_print_context(&state));
}

static int send_rpc(const char *msg, size_t len)
{
    if (!rpc_socket) return -1;

    int rc = zmq_send(rpc_socket, msg, len, ZMQ_DONTWAIT);
    if (rc < 0) {
        fprintf(stderr, "backend: send failed: %s\n", zmq_strerror(errno));
        return -1;
    }

    /* Wait for reply (with timeout) */
    zmq_pollitem_t items[] = {{rpc_socket, 0, ZMQ_POLLIN, 0}};
    rc = zmq_poll(items, 1, 1000); /* 1 second timeout */
    if (rc > 0) {
        zmq_msg_t reply;
        zmq_msg_init(&reply);
        zmq_msg_recv(&reply, rpc_socket, 0);
        zmq_msg_close(&reply);
    } else {
        if (rc == 0)
            fprintf(stderr, "backend: rpc reply timeout\n");
        else
            fprintf(stderr, "backend: rpc poll failed: %s\n", zmq_strerror(errno));
        open_rpc_socket();
        return -1;
    }

    return 0;
}

static int send_formatted_rpc(const char *msg, int len, size_t msg_size)
{
    if (len < 0 || (size_t)len >= msg_size) {
        fprintf(stderr, "backend: rpc payload too large\n");
        return -1;
    }

    return send_rpc(msg, (size_t)len);
}

int backend_send_gcode(const char *gcode)
{
    const char *lines[] = {gcode};
    return backend_send_gcodes(lines, 1);
}

int backend_send_gcodes(const char *const *gcodes, size_t count)
{
    char msg[256];
    int len = deneb_command_format_gcode(gcodes, count, msg, sizeof(msg));
    return send_formatted_rpc(msg, len, sizeof(msg));
}

int backend_send_macro(const char *macro)
{
    char msg[256];
    int len = deneb_command_format_macro(macro, msg, sizeof(msg));
    return send_formatted_rpc(msg, len, sizeof(msg));
}

int backend_send_job(const char *path, const char *source, const char *uuid,
                     float bed_target, float head_target)
{
    char msg[1024];
    int len;

    if (path && *path) {
        retain_print_filename(path);
        set_filename_or_none(state.filename, path);
        fprintf(stderr, "backend: job command path retained as active filename=%s\n",
                state.filename[0] ? state.filename : "(none)");
    }

    len = deneb_command_format_job(path, source, uuid, bed_target, head_target,
                                   msg, sizeof(msg));
    return send_formatted_rpc(msg, len, sizeof(msg));
}

static int pending_dispatch_start_allowed(void *ctx)
{
    (void)ctx;
    return backend_print_start_allowed();
}

static int pending_dispatch_abort(void *ctx)
{
    (void)ctx;
    return backend_abort_print();
}

static int pending_dispatch_job(void *ctx,
                                const deneb_print_job_start_plan_t *plan)
{
    (void)ctx;
    if (!plan)
        return -1;
    return backend_send_job(plan->path, plan->source, plan->uuid,
                            plan->bed_target, plan->nozzle_target);
}

int backend_send_pending_instruction(const char *instruction)
{
    deneb_pending_job_dispatch_ops_t ops = {
        NULL,
        pending_dispatch_start_allowed,
        pending_dispatch_abort,
        pending_dispatch_job
    };

    fprintf(stderr, "backend: send pending instruction=%s\n",
            instruction ? instruction : "(none)");
    return deneb_pending_job_dispatch_default(instruction, &ops);
}

int backend_send_command(const char *cmd, const char *args_json)
{
    char msg[1024];
    deneb_command_frame_plan_t plan;
    int len;

    if (!cmd || !*cmd)
        return -1;

    fprintf(stderr, "backend: command send cmd=%s args=%s\n", cmd, args_json ? args_json : "{}");
    len = deneb_command_plan_frame(cmd, args_json, msg, sizeof(msg), &plan);
    if (plan.has_job_path) {
        retain_print_filename(plan.job_path);
        set_filename_or_none(state.filename, plan.job_path);
        fprintf(stderr, "backend: job command path retained as active filename=%s\n",
                state.filename[0] ? state.filename : "(none)");
    }
    return send_formatted_rpc(msg, len, sizeof(msg));
}

int backend_abort_print(void)
{
    return backend_send_command(DENEB_COMMAND_VERB_ABORT, "{}");
}

int backend_pause_print(void)
{
    return backend_send_command(DENEB_COMMAND_VERB_PAUSE, "{}");
}

int backend_resume_print(void)
{
    return backend_send_command(DENEB_COMMAND_VERB_RESUME, "{}");
}

int backend_stop_print(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    long long now_ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    if (!deneb_print_stop_guard_begin(&stop_guard, now_ms))
        return 0;

    fprintf(stderr, "backend: stop print command requested\n");
    if (backend_send_command(DENEB_COMMAND_VERB_ABORT, "{}") != 0) {
        deneb_print_stop_guard_clear(&stop_guard);
        return -1;
    }
    deneb_pending_job_file_clear_default();
    return 0;
}

void backend_deinit(void)
{
    if (status_socket) {
        zmq_close(status_socket);
        status_socket = NULL;
    }
    if (rpc_socket) {
        zmq_close(rpc_socket);
        rpc_socket = NULL;
    }
    if (zmq_ctx) {
        zmq_ctx_destroy(zmq_ctx);
        zmq_ctx = NULL;
    }
}

#endif /* BACKEND_COMM_STUB */
