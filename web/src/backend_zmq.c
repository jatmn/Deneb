/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Backend ZMQ communication. Adapted from ui/src/backend_comm.c.
 * Connects to stock coordinator for printer status and commands.
 */

#include "backend_zmq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int backend_zmq_init(void) { fprintf(stderr, "backend_zmq: stub mode\n"); state.connected = true; return 0; }
int backend_zmq_get_fd(void) { return -1; }
void backend_zmq_poll(void) {}
const printer_state_t *backend_zmq_get_state(void) { return &state; }
const char *backend_zmq_get_status_json(void) { return "{}"; }
int backend_zmq_send_command(const char *cmd, const char *args) { (void)cmd; (void)args; return 0; }
int backend_zmq_send_gcode(const char *gcode) { (void)gcode; return 0; }
int backend_zmq_pause(void) { return 0; }
int backend_zmq_resume(void) { return 0; }
int backend_zmq_abort(void) { return 0; }
void backend_zmq_deinit(void) {}

#else /* ========== REAL ZMQ IMPLEMENTATION ========== */

#include <zmq.h>
#include <errno.h>

#define STATUS_URL   "tcp://127.0.0.1:5565"
#define RPC_URL      "tcp://127.0.0.1:5566"
#define STATUS_TOPIC "10001"
#define MAX_STATUS_MSGS 4

static void *zmq_ctx = NULL;
static void *status_sock = NULL;
static void *rpc_sock = NULL;
static printer_state_t state;
static char *status_json_cache = NULL;  /* pre-serialized JSON of last status */

static int create_rpc_socket(void)
{
    rpc_sock = zmq_socket(zmq_ctx, ZMQ_REQ);
    if (!rpc_sock) return -1;
    if (zmq_connect(rpc_sock, RPC_URL) < 0) {
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

/* Minimal JSON field extraction */
static int json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') { /* numeric or bool - find value */
        const char *end = p;
        while (*end && *end != ',' && *end != '}' && *end != '\n') end++;
        size_t len = (size_t)(end - p);
        if (len >= out_sz) len = out_sz - 1;
        memcpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    p++; /* skip opening quote */
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\') end++; /* skip escaped char */
        end++;
    }
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static float json_get_float(const char *json, const char *key, float def)
{
    char tmp[64];
    if (json_get_str(json, key, tmp, sizeof(tmp)) < 0) return def;
    return strtof(tmp, NULL);
}

static int json_get_int(const char *json, const char *key, int def)
{
    char tmp[64];
    if (json_get_str(json, key, tmp, sizeof(tmp)) < 0) return def;
    return atoi(tmp);
}

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int str_eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int str_is_one_of_ci(const char *s, const char *const *values)
{
    if (!s || !*s) return 0;
    for (int i = 0; values[i]; i++) {
        if (str_eq_ci(s, values[i])) return 1;
    }
    return 0;
}

static void json_escape_string(const char *src, char *dst, size_t dst_sz)
{
    size_t di = 0;
    if (dst_sz == 0) return;
    for (size_t si = 0; src && src[si] && di + 1 < dst_sz; si++) {
        char c = src[si];
        if ((c == '"' || c == '\\') && di + 2 < dst_sz) {
            dst[di++] = '\\';
            dst[di++] = c;
        } else if (c != '"' && c != '\\') {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
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
    const char *status = state.has_error ? "error" :
        (state.is_paused ? "paused" :
        (state.is_printing ? "printing" :
        (state.connected ? "idle" : "offline")));

    json_escape_string(state.filename, escaped_filename, sizeof(escaped_filename));

    n = snprintf(p, rem,
        "{\"nozzle_temp_cur\":%.1f,\"nozzle_temp_set\":%.1f,"
        "\"bed_temp_cur\":%.1f,\"bed_temp_set\":%.1f,"
        "\"pos_x\":%.1f,\"pos_y\":%.1f,\"pos_z\":%.1f,"
        "\"progress\":%.1f,\"time_total\":%d,\"time_left\":%d,"
        "\"filename\":\"%s\",\"status\":\"%s\","
        "\"is_printing\":%s,\"is_paused\":%s,\"has_error\":%s,"
        "\"connected\":%s}",
        state.nozzle_temp_cur, state.nozzle_temp_set,
        state.bed_temp_cur, state.bed_temp_set,
        state.pos_x, state.pos_y, state.pos_z,
        state.progress, state.time_total, state.time_left,
        escaped_filename,
        status,
        state.is_printing ? "true" : "false",
        state.is_paused ? "true" : "false",
        state.has_error ? "true" : "false",
        state.connected ? "true" : "false");

    p += n; rem -= n;

    (void)rem;
}

int backend_zmq_init(void)
{
    memset(&state, 0, sizeof(state));

    zmq_ctx = zmq_ctx_new();
    if (!zmq_ctx) return -1;

    /* Status SUB socket */
    status_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    if (!status_sock) goto fail;
    if (zmq_connect(status_sock, STATUS_URL) < 0) goto fail;
    if (zmq_setsockopt(status_sock, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC)) < 0) goto fail;

    /* RPC REQ socket */
    if (create_rpc_socket() < 0) goto fail;

    fprintf(stderr, "backend_zmq: connected to %s (status) and %s (rpc)\n", STATUS_URL, RPC_URL);
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
            json++; /* skip the '<' */

            state.nozzle_temp_cur = json_get_float(json, "headTcur", 0);
            state.nozzle_temp_set = json_get_float(json, "headTset", 0);
            state.bed_temp_cur = json_get_float(json, "bedTcur", 0);
            state.bed_temp_set = json_get_float(json, "bedTset", 0);
            state.pos_x = json_get_float(json, "X", 0);
            state.pos_y = json_get_float(json, "Y", 0);
            state.pos_z = json_get_float(json, "Z", 0);
            state.pos_e = json_get_float(json, "E", 0);
            state.time_total = json_get_int(json, "Ttot", 0);
            state.time_left = json_get_int(json, "Tleft", 0);

            if (state.time_total > 0 && state.time_total > state.time_left)
                state.progress = (float)(state.time_total - state.time_left) * 100.0f / (float)state.time_total;
            else
                state.progress = 0;

            state.filename[0] = '\0';
            state.source[0] = '\0';
            state.uuid[0] = '\0';
            state.current_req[0] = '\0';
            json_get_str(json, "file", state.filename, sizeof(state.filename));
            json_get_str(json, "source", state.source, sizeof(state.source));
            json_get_str(json, "uuid", state.uuid, sizeof(state.uuid));
            json_get_str(json, "req", state.current_req, sizeof(state.current_req));

            state.topcap_temp_cur = json_get_float(json, "topcapTemperature", 0);
            state.topcap_present = json_get_int(json, "topcapIsPresent", 0) ? true : false;

            /* Derive state flags */
            static const char *const printing_reqs[] = {
                "JOB", "Print", "Printing", NULL
            };
            static const char *const paused_reqs[] = {
                "PAUSE", "Pause", "Paused", NULL
            };
            int req_printing = str_is_one_of_ci(state.current_req, printing_reqs);
            int req_paused = str_is_one_of_ci(state.current_req, paused_reqs);
            int active_time = state.time_total > 0 &&
                              state.time_left > 0 &&
                              state.time_left <= state.time_total;

            state.is_paused = req_paused;
            state.is_printing = req_printing || req_paused || active_time;
            if (!state.is_printing) {
                state.time_total = 0;
                state.time_left = 0;
                state.progress = 0;
            }
            state.has_error = (json_get_int(json, "received_faults", 0) != 0);

            state.connected = true;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            state.last_update_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

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

int backend_zmq_send_command(const char *cmd, const char *args)
{
    if (!rpc_sock) return -1;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s<%s", cmd, args);
    int rc = zmq_send(rpc_sock, buf, strlen(buf), 0);
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

int backend_zmq_send_gcode(const char *gcode)
{
    /* Escape gcode for JSON string interpolation */
    char escaped[256];
    size_t ei = 0;
    for (size_t i = 0; gcode[i] && ei < sizeof(escaped) - 2; i++) {
        char c = gcode[i];
        if (c == '"' || c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = c; }
        else escaped[ei++] = c;
    }
    escaped[ei] = '\0';
    char args[280];
    snprintf(args, sizeof(args), "[\"%s\"]", escaped);
    return backend_zmq_send_command("GCODE", args);
}

int backend_zmq_pause(void)
{
    return backend_zmq_send_command("PAUSE", "{}");
}

int backend_zmq_resume(void)
{
    return backend_zmq_send_command("RESUME", "{}");
}

int backend_zmq_abort(void)
{
    return backend_zmq_send_command("ABORT", "{}");
}

void backend_zmq_deinit(void)
{
    if (status_sock) { zmq_close(status_sock); status_sock = NULL; }
    if (rpc_sock) { zmq_close(rpc_sock); rpc_sock = NULL; }
    if (zmq_ctx) { zmq_ctx_term(zmq_ctx); zmq_ctx = NULL; }
    if (status_json_cache) { free(status_json_cache); status_json_cache = NULL; }
}

#endif /* BACKEND_ZMQ_STUB */
