/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend ZMQ communication. Adapted from ui/src/backend_comm.c.
 * Connects to stock coordinator by default, or native deneb-printsvc when
 * the lab-gated native service is enabled.
 */

#include "backend_zmq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include "command_format.h"
#include "json_field.h"
#include "json_string.h"
#include "pending_job_file.h"
#include "print_backend_route.h"
#include "print_history.h"
#include "print_state_rules.h"
#include "status_payload.h"

#ifdef BACKEND_ZMQ_STUB

/* ========== STUB IMPLEMENTATION ========== */

static printer_state_t state = {
    .nozzle_temp_cur = 0, .nozzle_temp_set = 0,
    .bed_temp_cur = 0, .bed_temp_set = 0,
    .pos_x = 0, .pos_y = 0, .pos_z = 0, .pos_e = 0,
    .filename = "", .source = "", .uuid = "",
    .time_total = 0, .time_left = 0, .progress = 0,
    .is_printing = false, .is_paused = false, .has_error = false,
    .current_req = "", .connected = false, .last_update_ms = 0,
};
static deneb_print_backend_route_t backend_route = {
    DENEB_PRINT_BACKEND_COORDINATOR,
    DENEB_COORDINATOR_STATUS_URL,
    DENEB_COORDINATOR_COMMAND_URL
};

int backend_zmq_init(void) { fprintf(stderr, "backend_zmq: stub mode\n"); state.connected = true; return 0; }
int backend_zmq_get_fd(void) { return -1; }
void backend_zmq_poll(void) {}
const printer_state_t *backend_zmq_get_state(void) { return &state; }
const char *backend_zmq_get_status_json(void) { return "{}"; }
const char *backend_zmq_get_print_backend_name(void) { return deneb_print_backend_name(backend_route.backend); }
const char *backend_zmq_get_print_backend_status_url(void) { return backend_route.status_url; }
const char *backend_zmq_get_print_backend_command_url(void) { return backend_route.command_url; }
int backend_zmq_send_command(const char *cmd, const char *args) { (void)cmd; (void)args; return 0; }
int backend_zmq_send_gcode(const char *gcode) { (void)gcode; return 0; }
int backend_zmq_send_gcodes(const char *const *gcodes, size_t count) { (void)gcodes; (void)count; return 0; }
int backend_zmq_send_macro(const char *macro) { (void)macro; return 0; }
int backend_zmq_send_job(const char *path, const char *source,
                         const char *uuid, float bed_target,
                         float head_target)
{
    (void)path; (void)source; (void)uuid; (void)bed_target; (void)head_target;
    return 0;
}
int backend_zmq_pause(void) { return 0; }
int backend_zmq_resume(void) { return 0; }
int backend_zmq_abort(void) { return 0; }
int backend_zmq_stop_print(void) { return 0; }
void backend_zmq_deinit(void) {}

#else /* ========== REAL ZMQ IMPLEMENTATION ========== */

#include <zmq.h>
#include <errno.h>

#define STATUS_TOPIC "10001"
#define MAX_STATUS_MSGS 4
#define STOP_INFLIGHT_MS 3000

static deneb_print_backend_route_t backend_route;

static void *zmq_ctx = NULL;
static void *status_sock = NULL;
static void *rpc_sock = NULL;
static printer_state_t state;
static char *status_json_cache = NULL;  /* pre-serialized JSON of last status */
static int had_previous_status = 0;
static printer_state_t previous_state;
static int preheat_targets_logged = 0;
static int preheat_reached_logged = 0;
static time_t current_print_start_time = 0;
static char retained_print_filename[128];
static deneb_print_stop_guard_t stop_guard;

static int has_temp_targets(const printer_state_t *s)
{
    return deneb_print_has_temp_targets(s->bed_temp_set, s->nozzle_temp_set);
}

static int have_jobs_targets(const printer_state_t *s)
{
    return deneb_print_temp_targets_ready(s->bed_temp_cur, s->bed_temp_set,
                                          s->nozzle_temp_cur, s->nozzle_temp_set);
}

static void append_print_history(const printer_state_t *prev,
                                 const printer_state_t *curr);

static int state_has_print_context(const printer_state_t *s)
{
    deneb_print_observation_t obs;

    if (!s)
        return 0;
    obs.req = s->current_req;
    obs.file = s->filename;
    obs.time_total = s->time_total;
    obs.time_left = s->time_left;
    obs.bed_target = s->bed_temp_set;
    obs.nozzle_target = s->nozzle_temp_set;
    return deneb_print_has_active_context(&obs, s->is_printing,
                                          s->is_paused,
                                          deneb_print_file_is_candidate(s->filename));
}

static deneb_status_filename_context_t filename_context_from_state(
    const printer_state_t *s)
{
    deneb_status_filename_context_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    if (!s)
        return ctx;
    ctx.req = s->current_req;
    ctx.filename = s->filename;
    ctx.uuid = s->uuid;
    ctx.time_total = s->time_total;
    ctx.time_left = s->time_left;
    ctx.bed_target = s->bed_temp_set;
    ctx.nozzle_target = s->nozzle_temp_set;
    ctx.is_printing = s->is_printing;
    ctx.is_paused = s->is_paused;
    return ctx;
}

static void retain_backend_filename(const char *path)
{
    if (!path || !*path)
        return;

    deneb_pending_job_file_display_value(path, retained_print_filename,
                                         sizeof(retained_print_filename));
    deneb_pending_job_file_display_value(path, state.filename,
                                         sizeof(state.filename));
    fprintf(stderr, "backend_zmq: job command path retained as active filename=%s\n",
            state.filename[0] ? state.filename : "(none)");
}

static void log_status_transition(const printer_state_t *curr)
{
    if (!had_previous_status) {
        previous_state = *curr;
        had_previous_status = 1;
        return;
    }

    if (strcmp(previous_state.current_req, curr->current_req) != 0)
        fprintf(stderr, "deneb-api: printer req changed: \"%s\" -> \"%s\"\n",
                previous_state.current_req[0] ? previous_state.current_req : "none",
                curr->current_req[0] ? curr->current_req : "none");

    if (previous_state.is_paused && !curr->is_paused)
        fprintf(stderr, "deneb-api: print resumed (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");
    if (!previous_state.is_paused && curr->is_paused)
        fprintf(stderr, "deneb-api: print paused (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");

    if (previous_state.is_printing && !curr->is_printing) {
        if (curr->has_error)
            fprintf(stderr, "deneb-api: print ended with error (filename=%s)\n", previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else if (curr->time_total > 0 && curr->time_left <= 0)
            fprintf(stderr, "deneb-api: print completed (filename=%s)\n", previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else
            fprintf(stderr, "deneb-api: print stopped before completion (filename=%s)\n", previous_state.filename[0] ? previous_state.filename : "(unknown)");

        append_print_history(&previous_state, curr);
        preheat_targets_logged = 0;
        preheat_reached_logged = 0;
        if (deneb_pending_job_file_clear_default() == 0)
            fprintf(stderr, "deneb-api: removed pending job metadata after print end\n");
    }

    if (!previous_state.is_printing && curr->is_printing) {
        fprintf(stderr, "deneb-api: print started (filename=%s, req=%s)\n",
                curr->filename[0] ? curr->filename : "(unknown)",
                curr->current_req[0] ? curr->current_req : "unknown");
        current_print_start_time = time(NULL);
    }

    if (!preheat_targets_logged && has_temp_targets(curr)) {
        fprintf(stderr, "deneb-api: print preheating targets active: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_set, curr->nozzle_temp_set);
        preheat_targets_logged = 1;
    }

    if (preheat_targets_logged && !preheat_reached_logged && have_jobs_targets(curr)) {
        fprintf(stderr, "deneb-api: preheat targets reached: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_cur, curr->nozzle_temp_cur);
        preheat_reached_logged = 1;
    }

    if (!has_temp_targets(curr)) {
        preheat_targets_logged = 0;
        preheat_reached_logged = 0;
    }

    previous_state = *curr;
}

static void append_print_history(const printer_state_t *prev, const printer_state_t *curr)
{
    deneb_print_history_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.name = prev->filename;
    entry.uuid = prev->uuid;
    entry.source = prev->source;
    entry.state = deneb_print_completion_state_label(curr->has_error,
                                                     prev->time_total,
                                                     prev->time_left);
    entry.time_total = prev->time_total;
    entry.time_elapsed = deneb_print_elapsed_seconds(prev->time_total,
                                                     prev->time_left);
    entry.progress = prev->progress;
    entry.started_at = (long long)current_print_start_time;
    entry.finished_at = (long long)time(NULL);
    deneb_print_history_append_entry(DENEB_PRINT_HISTORY_PATH, &entry);
    current_print_start_time = 0;
}

static int create_rpc_socket(void)
{
    rpc_sock = zmq_socket(zmq_ctx, ZMQ_REQ);
    if (!rpc_sock) return -1;
    if (zmq_connect(rpc_sock, backend_route.command_url) < 0) {
        zmq_close(rpc_sock);
        rpc_sock = NULL;
        return -1;
    }

    int timeout = 1000;
    zmq_setsockopt(rpc_sock, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(rpc_sock, ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    return 0;
}

static void reset_rpc_socket(void)
{
    if (rpc_sock) {
        zmq_close(rpc_sock);
        rpc_sock = NULL;
    }
    if (zmq_ctx) create_rpc_socket();
}

static void update_status_cache(void)
{
    /* Pre-serialize the current state to JSON for fast serving */
    if (!status_json_cache) status_json_cache = malloc(1536);
    if (!status_json_cache) return;

    char *p = status_json_cache;
    int rem = 1536;
    int n;
    char escaped_filename[sizeof(state.filename) * 2 + 1];
    char route_fields[256];
    const char *status = deneb_print_status_label(state.connected,
        state.has_error, state.is_paused, state.is_printing);

    deneb_json_escape_string(state.filename, escaped_filename, sizeof(escaped_filename));
    if (deneb_print_backend_route_json_fields(&backend_route, route_fields,
                                              sizeof(route_fields)) < 0)
        snprintf(route_fields, sizeof(route_fields),
                 "\"print_backend\":\"unknown\"");

    n = snprintf(p, rem,
        "{\"nozzle_temp_cur\":%.1f,\"nozzle_temp_set\":%.1f,"
        "\"bed_temp_cur\":%.1f,\"bed_temp_set\":%.1f,"
        "\"pos_x\":%.1f,\"pos_y\":%.1f,\"pos_z\":%.1f,"
        "\"progress\":%.1f,\"time_total\":%d,\"time_left\":%d,"
        "\"filename\":\"%s\",\"status\":\"%s\","
        "\"is_printing\":%s,\"is_paused\":%s,\"has_error\":%s,"
        "\"connected\":%s,%s}",
        state.nozzle_temp_cur, state.nozzle_temp_set,
        state.bed_temp_cur, state.bed_temp_set,
        state.pos_x, state.pos_y, state.pos_z,
        state.progress, state.time_total, state.time_left,
        escaped_filename,
        status,
        state.is_printing ? "true" : "false",
        state.is_paused ? "true" : "false",
        state.has_error ? "true" : "false",
        state.connected ? "true" : "false",
        route_fields);

    p += n; rem -= n;

    (void)rem;
}

int backend_zmq_init(void)
{
    memset(&state, 0, sizeof(state));
    deneb_print_stop_guard_init(&stop_guard, STOP_INFLIGHT_MS);
    backend_route = deneb_print_backend_route_detect();

    zmq_ctx = zmq_ctx_new();
    if (!zmq_ctx) return -1;

    /* Status SUB socket */
    status_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    if (!status_sock) goto fail;
    if (zmq_connect(status_sock, backend_route.status_url) < 0) goto fail;
    if (zmq_setsockopt(status_sock, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC)) < 0) goto fail;

    /* RPC REQ socket */
    if (create_rpc_socket() < 0) goto fail;

    fprintf(stderr, "backend_zmq: selected %s print backend, connected to %s (status) and %s (rpc)\n",
            deneb_print_backend_name(backend_route.backend),
            backend_route.status_url, backend_route.command_url);
    state.connected = true;
    update_status_cache();
    return 0;

fail:
    backend_zmq_deinit();
    return -1;
}

int backend_zmq_get_fd(void)
{
    if (!status_sock) return -1;
    int fd = -1;
    size_t fd_size = sizeof(fd);
    zmq_getsockopt(status_sock, ZMQ_FD, &fd, &fd_size);
    return fd;
}

void backend_zmq_poll(void)
{
    if (!status_sock) return;

    for (int i = 0; i < MAX_STATUS_MSGS; i++) {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int rc = zmq_msg_recv(&msg, status_sock, ZMQ_DONTWAIT);
        if (rc < 0) {
            zmq_msg_close(&msg);
            break;
        }

        const char *data = (const char *)zmq_msg_data(&msg);
        size_t len = zmq_msg_size(&msg);

        /* Skip topic prefix "10001<" */
        const char *json = memchr(data, '<', len);
        if (json) {
            deneb_status_payload_t payload;
            printer_state_t prev = state;

            json++; /* skip the '<' */
            if (deneb_status_payload_parse(json, &payload) != 0) {
                zmq_msg_close(&msg);
                continue;
            }

            state.nozzle_temp_cur = payload.nozzle_temp_cur;
            state.nozzle_temp_set = payload.nozzle_temp_set;
            state.bed_temp_cur = payload.bed_temp_cur;
            state.bed_temp_set = payload.bed_temp_set;
            state.pos_x = payload.pos_x;
            state.pos_y = payload.pos_y;
            state.pos_z = payload.pos_z;
            state.pos_e = payload.pos_e;
            state.time_total = payload.time_total;
            state.time_left = payload.time_left;
            state.progress = payload.progress;
            snprintf(state.source, sizeof(state.source), "%s", payload.source);
            snprintf(state.uuid, sizeof(state.uuid), "%s", payload.uuid);
            snprintf(state.current_req, sizeof(state.current_req), "%s", payload.req);
            state.topcap_temp_cur = payload.topcap_temp_cur;
            state.topcap_present = payload.topcap_present != 0;
            state.is_paused = payload.is_paused != 0;
            state.is_printing = payload.is_printing != 0;
            {
                deneb_status_filename_context_t curr_ctx =
                    filename_context_from_state(&state);
                deneb_status_filename_context_t prev_ctx =
                    filename_context_from_state(&prev);
                deneb_status_payload_resolve_filename(&payload,
                                                      &curr_ctx,
                                                      &prev_ctx,
                                                      retained_print_filename,
                                                      sizeof(retained_print_filename),
                                                      state.filename,
                                                      sizeof(state.filename));
            }
            if (!state.is_printing) {
                state.time_total = 0;
                state.time_left = 0;
                state.progress = 0;
            }
            state.has_error = payload.has_error != 0;

            {
                deneb_status_filename_context_t curr_ctx =
                    filename_context_from_state(&state);
                deneb_status_filename_context_t prev_ctx =
                    filename_context_from_state(&prev);
                if (!deneb_status_payload_should_hold_filename(
                        &curr_ctx, &prev_ctx) &&
                state.time_total <= 0 &&
                state.time_left <= 0 &&
                state.bed_temp_set <= 0.0f &&
                state.nozzle_temp_set <= 0.0f &&
                state.current_req[0] == '\0' &&
                !state.is_printing &&
                !state.is_paused &&
                !prev.is_printing &&
                !prev.is_paused) {
                    deneb_print_stop_guard_clear(&stop_guard);
                }
            }

            state.connected = true;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            state.last_update_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

            log_status_transition(&state);
            update_status_cache();
        }

        zmq_msg_close(&msg);
    }
}

const printer_state_t *backend_zmq_get_state(void)
{
    return &state;
}

const char *backend_zmq_get_status_json(void)
{
    if (status_json_cache) return status_json_cache;
    return "{}";
}

const char *backend_zmq_get_print_backend_name(void)
{
    return deneb_print_backend_name(backend_route.backend);
}

const char *backend_zmq_get_print_backend_status_url(void)
{
    return backend_route.status_url;
}

const char *backend_zmq_get_print_backend_command_url(void)
{
    return backend_route.command_url;
}

static int backend_zmq_send_frame(const char *buf, size_t len)
{
    if (!rpc_sock) return -1;
    int rc = zmq_send(rpc_sock, buf, len, 0);
    if (rc < 0) {
        reset_rpc_socket();
        return -1;
    }

    /* Wait for reply */
    zmq_msg_t reply;
    zmq_msg_init(&reply);
    rc = zmq_msg_recv(&reply, rpc_sock, 0);
    zmq_msg_close(&reply);
    if (rc < 0) {
        reset_rpc_socket();
        return -1;
    }
    return 0;
}

int backend_zmq_send_command(const char *cmd, const char *args)
{
    char buf[4096];
    int len;

    if (!rpc_sock || !cmd || !*cmd) return -1;
    if (strcmp(cmd, DENEB_COMMAND_VERB_JOB) == 0 && args) {
        char path[256];
        if (deneb_command_extract_job_path(args, path, sizeof(path)) == 0)
            retain_backend_filename(path);
    }
    if (cmd &&
        (strcmp(cmd, DENEB_COMMAND_VERB_ABORT) == 0 ||
         strcmp(cmd, DENEB_COMMAND_VERB_PAUSE) == 0 ||
         strcmp(cmd, DENEB_COMMAND_VERB_RESUME) == 0) &&
        (!args || strcmp(args, "{}") == 0)) {
        len = deneb_command_format_action(cmd, buf, sizeof(buf));
    } else {
        len = deneb_command_format_raw(cmd, args, buf, sizeof(buf));
    }
    if (len < 0 || (size_t)len >= sizeof(buf)) {
        fprintf(stderr, "backend_zmq: rpc payload too large\n");
        return -1;
    }
    return backend_zmq_send_frame(buf, (size_t)len);
}

int backend_zmq_send_gcode(const char *gcode)
{
    const char *lines[] = {gcode};
    return backend_zmq_send_gcodes(lines, 1);
}

int backend_zmq_send_gcodes(const char *const *gcodes, size_t count)
{
    char buf[512];
    int len;

    len = deneb_command_format_gcode(gcodes, count, buf, sizeof(buf));
    if (len < 0) {
        fprintf(stderr, "backend_zmq: gcode payload too large\n");
        return -1;
    }

    return backend_zmq_send_frame(buf, (size_t)len);
}

int backend_zmq_send_macro(const char *macro)
{
    char buf[512];
    int len = deneb_command_format_macro(macro, buf, sizeof(buf));
    if (len < 0) {
        fprintf(stderr, "backend_zmq: macro payload too large\n");
        return -1;
    }
    return backend_zmq_send_frame(buf, (size_t)len);
}

int backend_zmq_send_job(const char *path, const char *source,
                         const char *uuid, float bed_target,
                         float head_target)
{
    char buf[4096];
    int len = deneb_command_format_job(path, source, uuid, bed_target,
                                       head_target, buf, sizeof(buf));
    if (len < 0) {
        fprintf(stderr, "backend_zmq: job payload too large\n");
        return -1;
    }
    retain_backend_filename(path);
    return backend_zmq_send_frame(buf, (size_t)len);
}

int backend_zmq_pause(void)
{
    return backend_zmq_send_command(DENEB_COMMAND_VERB_PAUSE, "{}");
}

int backend_zmq_resume(void)
{
    return backend_zmq_send_command(DENEB_COMMAND_VERB_RESUME, "{}");
}

int backend_zmq_abort(void)
{
    return backend_zmq_send_command(DENEB_COMMAND_VERB_ABORT, "{}");
}

int backend_zmq_stop_print(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    long long now_ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    if (deneb_print_stop_guard_inflight(&stop_guard, now_ms,
                                        state_has_print_context(&state)))
        return 0;
    if (!deneb_print_stop_guard_begin(&stop_guard, now_ms))
        return 0;

    fprintf(stderr, "deneb-api: stop print command requested\n");
    int rc = 0;

    if (backend_zmq_abort() < 0)
        rc = -1;

    if (rc == 0) {
        fprintf(stderr, "deneb-api: stop print command sent successfully\n");
    } else {
        deneb_print_stop_guard_clear(&stop_guard);
        fprintf(stderr, "deneb-api: stop print command completed with one or more failures\n");
    }

    return rc;
}

void backend_zmq_deinit(void)
{
    if (status_sock) { zmq_close(status_sock); status_sock = NULL; }
    if (rpc_sock) { zmq_close(rpc_sock); rpc_sock = NULL; }
    if (zmq_ctx) { zmq_ctx_term(zmq_ctx); zmq_ctx = NULL; }
    if (status_json_cache) { free(status_json_cache); status_json_cache = NULL; }
}

#endif /* BACKEND_ZMQ_STUB */
