/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend communication implementation.
 * Connects to stock coordinator via ZeroMQ.
 *
 * Build note: requires libzmq (static link for MIPS target).
 * For host testing without libzmq, define BACKEND_COMM_STUB to
 * compile a stub that returns dummy data.
 */

#include "backend_comm.h"
#include "command_format.h"
#include "pending_job_file.h"
#include "print_state_rules.h"

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
    .current_req = "", .connected = false, .last_update_ms = 0,
};

int backend_init(void) {
    fprintf(stderr, "backend: stub mode (no ZMQ)\n");
    state.connected = true;
    return 0;
}

void backend_poll(void) { /* no-op in stub */ }
const printer_state_t *backend_get_state(void) { return &state; }
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
int backend_send_command(const char *cmd, const char *args) { (void)cmd; (void)args; return 0; }
int backend_abort_print(void) { return 0; }
int backend_stop_print(void) { return 0; }
int backend_pause_print(void) { return 0; }
int backend_resume_print(void) { return 0; }
void backend_deinit(void) { /* no-op */ }

#else /* ========== REAL ZMQ IMPLEMENTATION ========== */

#include <zmq.h>
#include <errno.h>
#include <unistd.h>

/* Coordinator endpoints (from stock menu_settings.py) */
#define STATUS_URL   "tcp://127.0.0.1:5565"
#define RPC_URL      "tcp://127.0.0.1:5566"
#define STATUS_TOPIC "10001"
#define MAX_STATUS_MSGS_PER_POLL 4
#define STOP_INFLIGHT_MS 3000

static void *zmq_ctx = NULL;
static void *status_socket = NULL;  /* SUB - status from coordinator */
static void *rpc_socket = NULL;     /* REQ - commands to coordinator */

static printer_state_t state = {0};
static int had_previous_status = 0;
static printer_state_t previous_state = {0};
static int preheat_targets_logged = 0;
static int preheat_reached_logged = 0;
static char retained_print_filename[128];
static long long last_stop_ms = -1;
static int print_stop_inflight = 0;
static void set_filename_or_none(char *dst, const char *value);
static int configure_socket_linger(void *socket)
{
    int linger = 0;
    return zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
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

    if (zmq_connect(rpc_socket, RPC_URL) != 0) {
        fprintf(stderr, "backend: connect %s failed: %s\n", RPC_URL, zmq_strerror(errno));
        close_rpc_socket();
        return -1;
    }

    return 0;
}

/**
 * Minimal JSON value extractor for flat objects.
 * Looks for "key": value pairs in a JSON string.
 * Handles string values (with quotes) and numeric values.
 * Returns pointer to static buffer -- not thread safe (fine for single-thread UI).
 */
static const char *json_get_str(const char *json, const char *key)
{
    static char buf[256];
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *p = strstr(json, search);
    if (!p) return "";

    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '"') {
        /* String value */
        p++;
        const char *end = strchr(p, '"');
        if (!end) return "";
        size_t len = end - p;
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
        return buf;
    }

    /* Numeric value */
    const char *end = p;
    while (*end && *end != ',' && *end != '}' && *end != ' ' && *end != '\n')
        end++;
    size_t len = end - p;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

static float json_get_float(const char *json, const char *key)
{
    const char *val = json_get_str(json, key);
    if (!val || !*val) return 0.0f;
    return strtof(val, NULL);
}

static int json_get_int(const char *json, const char *key)
{
    const char *val = json_get_str(json, key);
    if (!val || !*val) return 0;
    return (int)strtol(val, NULL, 10);
}

static int str_is_one_of(const char *value, const char *const *choices)
{
    if (!value)
        return 0;

    for (int i = 0; choices[i]; i++) {
        if (strcmp(value, choices[i]) == 0)
            return 1;
    }

    return 0;
}

static int has_temp_targets(const printer_state_t *s)
{
    return deneb_print_has_temp_targets(s->bed_temp_set, s->nozzle_temp_set);
}

static int is_print_file_candidate(const char *file)
{
    return deneb_print_file_is_candidate(file);
}

static int state_has_print_context(const printer_state_t *s)
{
    deneb_print_observation_t obs;

    if (s->is_printing || s->is_paused)
        return 1;

    obs.req = s->current_req;
    obs.file = s->filename;
    obs.time_total = s->time_total;
    obs.time_left = s->time_left;
    obs.bed_target = s->bed_temp_set;
    obs.nozzle_target = s->nozzle_temp_set;
    return deneb_print_observation_has_context(&obs);
}

static int read_cluster_pending_name(char *out, size_t out_sz)
{
    deneb_pending_job_file_t job;

    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (deneb_pending_job_file_load_default(&job) != 0)
        return -1;

    if (job.name[0]) {
        set_filename_or_none(out, job.name);
        return 0;
    }

    if (job.path[0]) {
        set_filename_or_none(out, job.path);
        return 0;
    }

    return -1;
}

static void set_filename_or_none(char *dst, const char *value)
{
    if (!value || !*value || strcmp(value, "none") == 0) {
        dst[0] = '\0';
        return;
    }

    const char *base = strrchr(value, '/');
    if (base) {
        base++;
    } else {
        base = value;
    }

    if (!base || !*base || strcmp(base, "none") == 0) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, base, 127);
    dst[127] = '\0';
}

static int is_transient_print_file(const char *file)
{
    return deneb_print_file_is_transient(file);
}

static void retain_print_filename(const char *filename)
{
    if (!filename || !*filename || strcmp(filename, "none") == 0)
        return;

    const char *base = strrchr(filename, '/');
    if (base) {
        base++;
    } else {
        base = filename;
    }

    if (!base || !*base || strcmp(base, "none") == 0)
        return;

    strncpy(retained_print_filename, base, sizeof(retained_print_filename) - 1);
}

static int should_hold_print_filename(const printer_state_t *curr, const printer_state_t *prev)
{
    if (deneb_print_req_is_abort(curr->current_req))
        return 0;

    return (curr->is_printing || curr->is_paused ||
            prev->is_printing || prev->is_paused ||
            (curr->time_total > 0 && curr->time_left >= 0) ||
            (prev->time_total > 0 && prev->time_left >= 0) ||
            deneb_print_has_temp_targets(curr->bed_temp_set, curr->nozzle_temp_set) ||
            deneb_print_req_is_lifecycle(curr->current_req) ||
            deneb_print_req_is_lifecycle(prev->current_req) ||
            (curr->uuid[0] && prev->uuid[0] &&
             strcmp(curr->uuid, prev->uuid) == 0));
}

static int have_jobs_targets(const printer_state_t *s)
{
    return deneb_print_temp_targets_ready(s->bed_temp_cur, s->bed_temp_set,
                                          s->nozzle_temp_cur, s->nozzle_temp_set);
}

static void log_status_transition(const printer_state_t *curr)
{
    if (!had_previous_status) {
        previous_state = *curr;
        had_previous_status = 1;
        return;
    }

    if (strcmp(previous_state.current_req, curr->current_req) != 0)
        fprintf(stderr, "backend: printer req changed: \"%s\" -> \"%s\"\n",
                previous_state.current_req[0] ? previous_state.current_req : "none",
                curr->current_req[0] ? curr->current_req : "none");

    if (previous_state.is_paused && !curr->is_paused)
        fprintf(stderr, "backend: print resumed (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");
    if (!previous_state.is_paused && curr->is_paused)
        fprintf(stderr, "backend: print paused (filename=%s)\n", curr->filename[0] ? curr->filename : "(unknown)");

    if (previous_state.is_printing && !curr->is_printing) {
        if (curr->has_error)
            fprintf(stderr, "backend: print ended with error (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else if (curr->time_total > 0 && curr->time_left <= 0)
            fprintf(stderr, "backend: print completed (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        else
            fprintf(stderr, "backend: print stopped before completion (filename=%s)\n",
                    previous_state.filename[0] ? previous_state.filename : "(unknown)");
        retained_print_filename[0] = '\0';
    }

    if (!previous_state.is_printing && curr->is_printing)
        fprintf(stderr, "backend: print started (filename=%s, req=%s, uuid=%s, source=%s)\n",
                curr->filename[0] ? curr->filename : "(unknown)",
                curr->current_req[0] ? curr->current_req : "unknown",
                curr->uuid[0] ? curr->uuid : "(none)",
                curr->source[0] ? curr->source : "(none)");

    if (!preheat_targets_logged && has_temp_targets(curr)) {
        fprintf(stderr, "backend: print preheating targets active: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_set, curr->nozzle_temp_set);
        preheat_targets_logged = 1;
    }

    if (preheat_targets_logged && !preheat_reached_logged && have_jobs_targets(curr)) {
        fprintf(stderr, "backend: print preheat targets reached: bed=%0.1fC(nozzle=%0.1fC)\n",
                curr->bed_temp_cur, curr->nozzle_temp_cur);
        preheat_reached_logged = 1;
    }

    if (!has_temp_targets(curr)) {
        preheat_targets_logged = 0;
        preheat_reached_logged = 0;
    }

    previous_state = *curr;
}

/**
 * Parse a status JSON payload and update the cached state.
 */
static void parse_status(const char *json)
{
    printer_state_t prev = state;
    int was_printing = prev.is_printing || prev.is_paused;
    state.nozzle_temp_set = json_get_float(json, "headTset");
    state.nozzle_temp_cur = json_get_float(json, "headTcur");
    state.bed_temp_set = json_get_float(json, "bedTset");
    state.bed_temp_cur = json_get_float(json, "bedTcur");
    state.topcap_temp_cur = json_get_float(json, "topcapTemperature");
    const char *topcap = json_get_str(json, "topcapIsPresent");
    state.topcap_present = topcap &&
                            str_is_one_of(topcap,
                                          (const char *const[]){"yes", "true",
                                                               "t", "1",
                                                               NULL});
    state.pos_x = json_get_float(json, "X");
    state.pos_y = json_get_float(json, "Y");
    state.pos_z = json_get_float(json, "Z");
    state.pos_e = json_get_float(json, "E");
    state.time_total = json_get_int(json, "Ttot");
    state.time_left = json_get_int(json, "Tleft");

    char file_buf[256];
    char name_buf[256];
    snprintf(file_buf, sizeof(file_buf), "%s", json_get_str(json, "file"));
    snprintf(name_buf, sizeof(name_buf), "%s", json_get_str(json, "name"));
    const char *file = file_buf;
    const char *name = name_buf;
    if ((!file || !*file || strcmp(file, "none") == 0) &&
        name && strcmp(name, "none") != 0 && *name) {
        file = name;
    }
    int has_file = file && *file && strcmp(file, "none") != 0;
    const char *src = json_get_str(json, "source");
    if (src && *src)
        strncpy(state.source, src, sizeof(state.source) - 1);
    else
        state.source[0] = '\0';

    const char *uuid = json_get_str(json, "uuid");
    if (uuid && *uuid)
        strncpy(state.uuid, uuid, sizeof(state.uuid) - 1);
    else
        state.uuid[0] = '\0';

    const char *req = json_get_str(json, "req");
    if (req && *req)
        strncpy(state.current_req, req, sizeof(state.current_req) - 1);
    else
        state.current_req[0] = '\0';

    /* Calculate progress from time */
    if (state.time_total > 0) {
        state.progress = (float)(state.time_total - state.time_left)
                         / (float)state.time_total * 100.0f;
    }

    deneb_print_observation_t obs;
    obs.req = state.current_req;
    obs.file = file;
    obs.time_total = state.time_total;
    obs.time_left = state.time_left;
    obs.bed_target = state.bed_temp_set;
    obs.nozzle_target = state.nozzle_temp_set;

    /* Derive state flags */
    if (deneb_print_req_is_abort(state.current_req)) {
        state.is_printing = false;
    } else {
        state.is_printing = deneb_print_observation_has_context(&obs);
    }
    state.is_paused = deneb_print_req_is_paused(state.current_req);

    char pending_name[128];
    int has_pending_name = (read_cluster_pending_name(pending_name, sizeof(pending_name)) == 0 && pending_name[0] != '\0');
    char original_file[128];

    if (has_file) {
        const char *base_file = strrchr(file, '/');
        base_file = base_file ? (base_file + 1) : file;
        strncpy(original_file, base_file, sizeof(original_file) - 1);
        original_file[sizeof(original_file) - 1] = '\0';

        if (is_transient_print_file(file)) {
            if (has_pending_name) {
                set_filename_or_none(state.filename, pending_name);
            } else if (retained_print_filename[0]) {
                set_filename_or_none(state.filename, retained_print_filename);
            } else if (prev.filename[0] && is_print_file_candidate(prev.filename)) {
                set_filename_or_none(state.filename, prev.filename);
            }
            if (state.filename[0] && strcmp(state.filename, original_file) != 0)
                fprintf(stderr, "backend: ignored transient print file \"%s\" and kept \"%s\" as active name\n",
                        original_file, state.filename);
        } else if (!is_print_file_candidate(file)) {
            if (retained_print_filename[0] && (!state.filename[0] || !is_print_file_candidate(state.filename))) {
                set_filename_or_none(state.filename, retained_print_filename);
            } else if (prev.filename[0] && is_print_file_candidate(prev.filename)) {
                set_filename_or_none(state.filename, prev.filename);
            }
        } else {
            set_filename_or_none(state.filename, file);
            retain_print_filename(file);
        }
    } else if (should_hold_print_filename(&state, &prev) && has_pending_name) {
        set_filename_or_none(state.filename, pending_name);
    } else if (retained_print_filename[0]) {
        set_filename_or_none(state.filename, retained_print_filename);
    } else {
        state.filename[0] = '\0';
    }

    if (should_hold_print_filename(&state, &prev)) {
        if (!state.filename[0] && retained_print_filename[0]) {
            set_filename_or_none(state.filename, retained_print_filename);
            if (state.filename[0])
                fprintf(stderr, "backend: restored retained print filename during lifecycle: \"%s\" (req=%s)\n",
                        state.filename, state.current_req[0] ? state.current_req : "none");
        }

        if (!state.filename[0] && prev.filename[0] && is_print_file_candidate(prev.filename)) {
            set_filename_or_none(state.filename, prev.filename);
            if (state.filename[0])
                fprintf(stderr, "backend: keeping previous print filename during lifecycle: \"%s\" (req=%s)\n",
                        state.filename, state.current_req[0] ? state.current_req : "none");
        }
    } else if (deneb_print_req_is_abort(state.current_req) ||
               (was_printing && !state.is_printing && !state.is_paused)) {
        state.filename[0] = '\0';
        retained_print_filename[0] = '\0';
    } else if (!state.filename[0]) {
        state.filename[0] = '\0';
    }

    if (state.filename[0] == '\0' &&
        !has_file &&
        !should_hold_print_filename(&state, &prev) &&
        state.bed_temp_set == 0.0f &&
        state.nozzle_temp_set == 0.0f) {
        retained_print_filename[0] = '\0';
    }

    if (!should_hold_print_filename(&state, &prev) &&
        state.time_total <= 0 &&
        state.time_left <= 0 &&
        state.bed_temp_set <= 0.0f &&
        state.nozzle_temp_set <= 0.0f &&
        state.current_req[0] == '\0' &&
        !state.is_printing &&
        !state.is_paused &&
        !prev.is_printing &&
        !prev.is_paused) {
        print_stop_inflight = 0;
    }

    if (state.time_total > 0 && state.time_left >= 0 && !state.is_printing && !state.is_paused) {
        state.time_total = 0;
        state.time_left = 0;
        state.progress = 0;
    }

    if (state.time_total > 0 && state.time_left > state.time_total)
        state.time_left = state.time_total;

    state.has_error = (json_get_int(json, "received_faults") != 0);

    log_status_transition(&state);

    /* Update timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    state.last_update_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    state.connected = true;
}

int backend_init(void)
{
    memset(&state, 0, sizeof(state));

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
    if (zmq_connect(status_socket, STATUS_URL) != 0) {
        fprintf(stderr, "backend: connect %s failed: %s\n", STATUS_URL, zmq_strerror(errno));
        return -1;
    }
    zmq_setsockopt(status_socket, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC));

    /* REQ socket for commands */
    if (open_rpc_socket() != 0)
        return -1;

    /* Set linger to 0 so we don't block on shutdown */
    configure_socket_linger(status_socket);

    fprintf(stderr, "backend: connected to %s (status) and %s (rpc)\n",
            STATUS_URL, RPC_URL);

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
int backend_is_stop_print_inflight(void)
{
    if (!print_stop_inflight || last_stop_ms < 0)
        return 0;

    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return print_stop_inflight;

    long long now_ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    if ((now_ms - last_stop_ms) < STOP_INFLIGHT_MS)
        return 1;

    if (state_has_print_context(&state))
        return 1;

    print_stop_inflight = 0;
    return 0;
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

int backend_send_command(const char *cmd, const char *args_json)
{
    char msg[1024];
    int len;

    if (!cmd || !*cmd)
        return -1;

    if (cmd && strcmp(cmd, "JOB") == 0 && args_json) {
        const char *path = json_get_str(args_json, "path");
        if (!path || !*path || strcmp(path, "none") == 0)
            path = json_get_str(args_json, "file");
        if (path && *path && strcmp(path, "none") != 0) {
            retain_print_filename(path);
            set_filename_or_none(state.filename, path);
            fprintf(stderr, "backend: job command path retained as active filename=%s\n",
                    state.filename[0] ? state.filename : "(none)");
        }
    }

    fprintf(stderr, "backend: command send cmd=%s args=%s\n", cmd, args_json ? args_json : "{}");
    if ((strcmp(cmd, "ABORT") == 0 || strcmp(cmd, "PAUSE") == 0 || strcmp(cmd, "RESUME") == 0) &&
        (!args_json || strcmp(args_json, "{}") == 0)) {
        len = deneb_command_format_action(cmd, msg, sizeof(msg));
    } else {
        len = snprintf(msg, sizeof(msg), "%s<%s", cmd, args_json ? args_json : "{}");
    }
    return send_formatted_rpc(msg, len, sizeof(msg));
}

int backend_abort_print(void)
{
    return backend_send_command("ABORT", "{}");
}

int backend_pause_print(void)
{
    return backend_send_command("PAUSE", "{}");
}

int backend_resume_print(void)
{
    return backend_send_command("RESUME", "{}");
}

int backend_stop_print(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    long long now_ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    if (print_stop_inflight && last_stop_ms >= 0 &&
        (now_ms - last_stop_ms) < STOP_INFLIGHT_MS)
        return 0;

    if (print_stop_inflight && !backend_is_stop_print_inflight())
        print_stop_inflight = 0;
    if (print_stop_inflight)
        return 0;

    print_stop_inflight = 1;
    last_stop_ms = now_ms;

    fprintf(stderr, "backend: stop print command requested\n");
    if (backend_send_command("ABORT", "{}") != 0) {
        print_stop_inflight = 0;
        return -1;
    }
    unlink(DENEB_PENDING_JOB_PATH);
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
