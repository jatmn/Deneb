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
int backend_send_gcode(const char *gcode) { (void)gcode; return 0; }
int backend_send_command(const char *cmd, const char *args) { (void)cmd; (void)args; return 0; }
int backend_abort_print(void) { return 0; }
int backend_pause_print(void) { return 0; }
int backend_resume_print(void) { return 0; }
void backend_deinit(void) { /* no-op */ }

#else /* ========== REAL ZMQ IMPLEMENTATION ========== */

#include <zmq.h>
#include <errno.h>

/* Coordinator endpoints (from stock menu_settings.py) */
#define STATUS_URL   "tcp://127.0.0.1:5565"
#define RPC_URL      "tcp://127.0.0.1:5566"
#define STATUS_TOPIC "10001"
#define MAX_STATUS_MSGS_PER_POLL 4

static void *zmq_ctx = NULL;
static void *status_socket = NULL;  /* SUB - status from coordinator */
static void *rpc_socket = NULL;     /* REQ - commands to coordinator */

static printer_state_t state = {0};

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

/**
 * Parse a status JSON payload and update the cached state.
 */
static void parse_status(const char *json)
{
    state.nozzle_temp_set = json_get_float(json, "headTset");
    state.nozzle_temp_cur = json_get_float(json, "headTcur");
    state.bed_temp_set = json_get_float(json, "bedTset");
    state.bed_temp_cur = json_get_float(json, "bedTcur");
    state.pos_x = json_get_float(json, "X");
    state.pos_y = json_get_float(json, "Y");
    state.pos_z = json_get_float(json, "Z");
    state.pos_e = json_get_float(json, "E");
    state.time_total = json_get_int(json, "Ttot");
    state.time_left = json_get_int(json, "Tleft");

    const char *file = json_get_str(json, "file");
    if (file && *file)
        strncpy(state.filename, file, sizeof(state.filename) - 1);

    const char *src = json_get_str(json, "source");
    if (src && *src)
        strncpy(state.source, src, sizeof(state.source) - 1);

    const char *uuid = json_get_str(json, "uuid");
    if (uuid && *uuid)
        strncpy(state.uuid, uuid, sizeof(state.uuid) - 1);

    const char *req = json_get_str(json, "req");
    if (req && *req)
        strncpy(state.current_req, req, sizeof(state.current_req) - 1);

    /* Calculate progress from time */
    if (state.time_total > 0) {
        state.progress = (float)(state.time_total - state.time_left)
                         / (float)state.time_total * 100.0f;
    }

    static const char *const printing_reqs[] = {"JOB", "Print", "Printing", NULL};
    static const char *const paused_reqs[] = {"PAUSE", "Pause", "Paused", NULL};

    /* Derive state flags */
    state.is_printing = str_is_one_of(state.current_req, printing_reqs) ||
                        (state.filename[0] != '\0' &&
                         strcmp(state.filename, "none") != 0);
    state.is_paused = str_is_one_of(state.current_req, paused_reqs);

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
    /* Format: GCODE<["M140 S60"] */
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "GCODE<[\"%s\"]", gcode);
    return send_formatted_rpc(msg, len, sizeof(msg));
}

int backend_send_command(const char *cmd, const char *args_json)
{
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "%s<%s", cmd, args_json);
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
