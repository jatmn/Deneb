/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb Digital Factory Bridge - C replacement for deneb-df-bridge.py
 *
 * The stock Digital Factory pairing flow lives behind Gershwin IPC rather
 * than the legacy print-command ZMQ socket. This helper sends the same
 * coordinator requests as the stock menu and prints a compact status line
 * for the C UI to read from stdout.
 *
 * Usage:
 *   deneb-df-bridge connect [--timeout 20]
 *   deneb-df-bridge disconnect [--timeout 20]
 *   deneb-df-bridge status [--timeout 20]
 *
 * Build (host):
 *   gcc -O2 -o deneb-df-bridge deneb-df-bridge.c -lzmq
 *
 * The release package links this into deneb-ui and installs
 * /usr/bin/deneb-df-bridge as a symlink, avoiding a second static ZMQ binary.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <zmq.h>

/* ---------- Constants matching the Python bridge ---------- */

#define IPC_BASE          "tcp://127.0.0.1:"
#define GERSHWIN_PUB_BASE 5546
/* Deneb replaces the stock Cygnus menu and reuses the stock GUI pubinstance.
 * Coordinator and Digital Factory subscribe only to pubbase slots 5546-5549. */
#define DENEB_GUI_PUB_PORT 5547
#define SOURCE             "deneb/df-bridge"

/* DigitalFactoryInstruction enum values (from stock marshal types) */
#define DF_INSTR_NONE             0
#define DF_INSTR_CONNECT          1
#define DF_INSTR_DISCONNECT       2

/* DigitalFactoryState enum values */
#define DF_STATE_DISCONNECTED     0
#define DF_STATE_ENTER_PIN        1
#define DF_STATE_CONNECTED        2
#define DF_STATE_RECONNECTING     3

/* ---------- Minimal msgpack encoder ---------- */

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} mp_buf_t;

static void mp_init(mp_buf_t *mp)
{
    mp->buf = NULL;
    mp->len = 0;
    mp->cap = 0;
}

static void mp_free(mp_buf_t *mp)
{
    free(mp->buf);
    mp->buf = NULL;
    mp->len = 0;
    mp->cap = 0;
}

static int mp_ensure(mp_buf_t *mp, size_t need)
{
    if (mp->len + need <= mp->cap)
        return 0;
    size_t new_cap = mp->cap ? mp->cap * 2 : 256;
    while (new_cap < mp->len + need)
        new_cap *= 2;
    uint8_t *p = realloc(mp->buf, new_cap);
    if (!p) return -1;
    mp->buf = p;
    mp->cap = new_cap;
    return 0;
}

static int mp_write(mp_buf_t *mp, const void *data, size_t len)
{
    if (mp_ensure(mp, len) < 0) return -1;
    memcpy(mp->buf + mp->len, data, len);
    mp->len += len;
    return 0;
}

static int mp_put_u8(mp_buf_t *mp, uint8_t v)
{
    return mp_write(mp, &v, 1);
}

static int mp_put_u16(mp_buf_t *mp, uint16_t v)
{
    uint8_t d[3] = {0xcd, (uint8_t)(v >> 8), (uint8_t)v};
    return mp_write(mp, d, 3);
}

static int mp_put_u32(mp_buf_t *mp, uint32_t v)
{
    uint8_t d[5] = {0xce,
                    (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8),  (uint8_t)v};
    return mp_write(mp, d, 5);
}

static int mp_put_i64(mp_buf_t *mp, int64_t v)
{
    uint8_t d[9] = {0xd3,
                    (uint8_t)(v >> 56), (uint8_t)(v >> 48),
                    (uint8_t)(v >> 40), (uint8_t)(v >> 32),
                    (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8),  (uint8_t)v};
    return mp_write(mp, d, 9);
}

static int mp_put_nil(mp_buf_t *mp)
{
    return mp_put_u8(mp, 0xc0);
}

static int mp_put_str(mp_buf_t *mp, const char *s)
{
    size_t len = strlen(s);
    if (len <= 31) {
        if (mp_put_u8(mp, (uint8_t)(0xa0 | len)) < 0) return -1;
    } else if (len <= 255) {
        if (mp_put_u8(mp, 0xd9) < 0) return -1;
        if (mp_put_u8(mp, (uint8_t)len) < 0) return -1;
    } else if (len <= 65535) {
        if (mp_put_u8(mp, 0xda) < 0) return -1;
        if (mp_put_u16(mp, (uint16_t)len) < 0) return -1;
    } else {
        if (mp_put_u8(mp, 0xdb) < 0) return -1;
        if (mp_put_u32(mp, (uint32_t)len) < 0) return -1;
    }
    return mp_write(mp, s, len);
}

/* Write a fixmap header (up to 15 elements) */
static int mp_put_map_header(mp_buf_t *mp, uint8_t count)
{
    return mp_put_u8(mp, (uint8_t)(0x80 | (count & 0x0f)));
}

/* Encode a DigitalFactoryRequest as msgpack.
 * Format: {tracker: int, instruction: int, data: nil, __class__: "DigitalFactoryRequest"}
 */
static int mp_encode_df_request(mp_buf_t *mp, int tracker, int instruction)
{
    /* 4 fields */
    if (mp_put_map_header(mp, 4) < 0) return -1;

    /* tracker */
    if (mp_put_str(mp, "tracker") < 0) return -1;
    if (tracker >= 0 && tracker <= 127) {
        if (mp_put_u8(mp, (uint8_t)tracker) < 0) return -1;
    } else {
        if (mp_put_i64(mp, tracker) < 0) return -1;
    }

    /* instruction */
    if (mp_put_str(mp, "instruction") < 0) return -1;
    if (mp_put_u8(mp, (uint8_t)instruction) < 0) return -1;

    /* data (nil) */
    if (mp_put_str(mp, "data") < 0) return -1;
    if (mp_put_nil(mp) < 0) return -1;

    /* __class__ */
    if (mp_put_str(mp, "__class__") < 0) return -1;
    if (mp_put_str(mp, "DigitalFactoryRequest") < 0) return -1;

    return 0;
}

/* Encode a Gershwin IPC key dict as msgpack.
 * Format: {ts: int, action: str, source: str, target: str}
 */
static int mp_encode_key(mp_buf_t *mp, int64_t ts, const char *action,
                         const char *source, const char *target)
{
    /* 4 fields */
    if (mp_put_map_header(mp, 4) < 0) return -1;

    if (mp_put_str(mp, "ts") < 0) return -1;
    if (ts >= 0 && ts <= 127) {
        if (mp_put_u8(mp, (uint8_t)ts) < 0) return -1;
    } else {
        if (mp_put_i64(mp, ts) < 0) return -1;
    }

    if (mp_put_str(mp, "action") < 0) return -1;
    if (mp_put_str(mp, action) < 0) return -1;

    if (mp_put_str(mp, "source") < 0) return -1;
    if (mp_put_str(mp, source) < 0) return -1;

    if (mp_put_str(mp, "target") < 0) return -1;
    if (mp_put_str(mp, target) < 0) return -1;

    return 0;
}

/* ---------- Minimal msgpack decoder (for reading status responses) ---------- */

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
} mp_reader_t;

static void mp_reader_init(mp_reader_t *r, const void *data, size_t len)
{
    r->buf = (const uint8_t *)data;
    r->len = len;
    r->pos = 0;
}

static int mp_read_u8(mp_reader_t *r, uint8_t *out)
{
    if (r->pos >= r->len) return -1;
    *out = r->buf[r->pos++];
    return 0;
}

static int mp_read_bytes(mp_reader_t *r, void *out, size_t n)
{
    if (r->pos + n > r->len) return -1;
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return 0;
}

/* Skip one msgpack value (for fields we don't care about) */
static int mp_skip_value(mp_reader_t *r);

static int mp_skip_n(mp_reader_t *r, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (mp_skip_value(r) < 0) return -1;
    }
    return 0;
}

static int mp_skip_value(mp_reader_t *r)
{
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0) return -1;

    if (tag <= 0x7f || tag >= 0xe0) {
        /* fixint positive or fixint negative: no payload */
        return 0;
    }
    if (tag >= 0xa0 && tag <= 0xbf) {
        /* fixstr */
        size_t len = tag & 0x1f;
        r->pos += len;
        if (r->pos > r->len) return -1;
        return 0;
    }
    if (tag >= 0x80 && tag <= 0x8f) {
        /* fixmap */
        size_t n = tag & 0x0f;
        return mp_skip_n(r, n * 2);
    }
    if (tag >= 0x90 && tag <= 0x9f) {
        /* fixarray */
        size_t n = tag & 0x0f;
        return mp_skip_n(r, n);
    }

    switch (tag) {
    case 0xc0: /* nil */
    case 0xc2: /* false */
    case 0xc3: /* true */
        return 0;
    case 0xca: { /* float32 */
        uint8_t d[4]; return mp_read_bytes(r, d, 4);
    }
    case 0xcb: { /* float64 */
        uint8_t d[8]; return mp_read_bytes(r, d, 8);
    }
    case 0xcc: { /* uint8 */
        uint8_t v; return mp_read_u8(r, &v);
    }
    case 0xcd: { /* uint16 */
        uint8_t d[2]; return mp_read_bytes(r, d, 2);
    }
    case 0xce: { /* uint32 */
        uint8_t d[4]; return mp_read_bytes(r, d, 4);
    }
    case 0xcf: { /* uint64 */
        uint8_t d[8]; return mp_read_bytes(r, d, 8);
    }
    case 0xd0: { /* int8 */
        uint8_t v; return mp_read_u8(r, &v);
    }
    case 0xd1: { /* int16 */
        uint8_t d[2]; return mp_read_bytes(r, d, 2);
    }
    case 0xd2: { /* int32 */
        uint8_t d[4]; return mp_read_bytes(r, d, 4);
    }
    case 0xd3: { /* int64 */
        uint8_t d[8]; return mp_read_bytes(r, d, 8);
    }
    case 0xd9: { /* str8 */
        uint8_t len; if (mp_read_u8(r, &len) < 0) return -1;
        r->pos += len;
        if (r->pos > r->len) return -1;
        return 0;
    }
    case 0xda: { /* str16 */
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) return -1;
        size_t len = ((size_t)d[0] << 8) | d[1];
        r->pos += len;
        if (r->pos > r->len) return -1;
        return 0;
    }
    case 0xdb: { /* str32 */
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) return -1;
        size_t len = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) |
                     ((size_t)d[2] << 8) | d[3];
        r->pos += len;
        if (r->pos > r->len) return -1;
        return 0;
    }
    case 0xdc: { /* array16 */
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) return -1;
        size_t n = ((size_t)d[0] << 8) | d[1];
        return mp_skip_n(r, n);
    }
    case 0xdd: { /* array32 */
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) return -1;
        size_t n = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) |
                   ((size_t)d[2] << 8) | d[3];
        return mp_skip_n(r, n);
    }
    case 0xde: { /* map16 */
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) return -1;
        size_t n = ((size_t)d[0] << 8) | d[1];
        return mp_skip_n(r, n * 2);
    }
    case 0xdf: { /* map32 */
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) return -1;
        size_t n = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) |
                   ((size_t)d[2] << 8) | d[3];
        return mp_skip_n(r, n * 2);
    }
    default:
        return -1; /* unknown type */
    }
}

/* Read a string value from the reader. Caller must free *out.
 * Returns 0 on success, -1 if not a string or read error.
 * On failure, the reader position is NOT rewound. */
static int mp_read_str_alloc(mp_reader_t *r, char **out)
{
    size_t start = r->pos;
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0) return -1;

    size_t len = 0;
    if (tag >= 0xa0 && tag <= 0xbf) {
        len = tag & 0x1f;
    } else if (tag == 0xd9) {
        uint8_t l; if (mp_read_u8(r, &l) < 0) goto fail;
        len = l;
    } else if (tag == 0xda) {
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) goto fail;
        len = ((size_t)d[0] << 8) | d[1];
    } else if (tag == 0xdb) {
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) goto fail;
        len = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) |
              ((size_t)d[2] << 8) | d[3];
    } else {
        goto fail;
    }

    if (r->pos + len > r->len) goto fail;

    *out = malloc(len + 1);
    if (!*out) goto fail;
    memcpy(*out, r->buf + r->pos, len);
    (*out)[len] = '\0';
    r->pos += len;
    return 0;

fail:
    r->pos = start;
    return -1;
}

/* Read an integer value. Returns 0 on success, -1 if not an int. */
static int mp_read_int(mp_reader_t *r, int64_t *out)
{
    size_t start = r->pos;
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0) return -1;

    if (tag <= 0x7f) {
        *out = tag;
        return 0;
    }
    if (tag >= 0xe0) {
        *out = (int8_t)tag; /* negative fixint */
        return 0;
    }

    switch (tag) {
    case 0xcc: {
        uint8_t v; if (mp_read_u8(r, &v) < 0) goto fail;
        *out = v; return 0;
    }
    case 0xcd: {
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) goto fail;
        *out = ((uint16_t)d[0] << 8) | d[1]; return 0;
    }
    case 0xce: {
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) goto fail;
        *out = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
               ((uint32_t)d[2] << 8) | d[3]; return 0;
    }
    case 0xcf: {
        uint8_t d[8]; if (mp_read_bytes(r, d, 8) < 0) goto fail;
        *out = (int64_t)(((uint64_t)d[0] << 56) | ((uint64_t)d[1] << 48) |
               ((uint64_t)d[2] << 40) | ((uint64_t)d[3] << 32) |
               ((uint64_t)d[4] << 24) | ((uint64_t)d[5] << 16) |
               ((uint64_t)d[6] << 8) | d[7]); return 0;
    }
    case 0xd0: {
        uint8_t v; if (mp_read_u8(r, &v) < 0) goto fail;
        *out = (int8_t)v; return 0;
    }
    case 0xd1: {
        uint8_t d[2]; if (mp_read_bytes(r, d, 2) < 0) goto fail;
        *out = (int16_t)(((uint16_t)d[0] << 8) | d[1]); return 0;
    }
    case 0xd2: {
        uint8_t d[4]; if (mp_read_bytes(r, d, 4) < 0) goto fail;
        *out = (int32_t)(((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
               ((uint32_t)d[2] << 8) | d[3]); return 0;
    }
    case 0xd3: {
        uint8_t d[8]; if (mp_read_bytes(r, d, 8) < 0) goto fail;
        *out = (int64_t)(((uint64_t)d[0] << 56) | ((uint64_t)d[1] << 48) |
               ((uint64_t)d[2] << 40) | ((uint64_t)d[3] << 32) |
               ((uint64_t)d[4] << 24) | ((uint64_t)d[5] << 16) |
               ((uint64_t)d[6] << 8) | d[7]); return 0;
    }
    }

fail:
    r->pos = start;
    return -1;
}

/* Read a bool, accepting msgpack bools and integer 0/1 for compatibility. */
static int mp_read_bool(mp_reader_t *r, bool *out)
{
    size_t start = r->pos;
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0) return -1;

    if (tag == 0xc2 || tag == 0xc3) {
        *out = (tag == 0xc3);
        return 0;
    }

    r->pos = start;
    int64_t val;
    if (mp_read_int(r, &val) == 0) {
        *out = (val != 0);
        return 0;
    }

    r->pos = start;
    return -1;
}

/* Find a key in a fixmap and position reader at its value.
 * Returns 0 found, -1 not found. Reader positioned after map header on entry,
 * at value start on success, or at end of map on failure. */
static int mp_map_find_key(mp_reader_t *r, uint8_t map_size, const char *key)
{
    for (uint8_t i = 0; i < map_size; i++) {
        char *k = NULL;
        if (mp_read_str_alloc(r, &k) < 0) return -1;
        int match = (strcmp(k, key) == 0);
        free(k);
        if (match) return 0;
        /* skip the value */
        if (mp_skip_value(r) < 0) return -1;
    }
    return -1;
}

/* ---------- Time helpers ---------- */

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------- Main logic ---------- */

static const char *state_name(int state)
{
    switch (state) {
    case DF_STATE_DISCONNECTED: return "disconnected";
    case DF_STATE_ENTER_PIN:    return "enter_pin";
    case DF_STATE_CONNECTED:    return "connected";
    case DF_STATE_RECONNECTING: return "reconnecting";
    default:                    return "unknown";
    }
}

static void format_status(bool has_accepted, bool accepted,
                          bool has_state, int state,
                          bool has_pin, const char *pin)
{
    bool first = true;

    if (has_accepted) {
        printf("accepted=%d", accepted ? 1 : 0);
        first = false;
    }
    if (has_state) {
        if (!first) putchar(' ');
        printf("state=%s", state_name(state));
        first = false;
    }
    if (has_pin && pin && pin[0]) {
        if (!first) putchar(' ');
        printf("pin=%s", pin);
        first = false;
    }
    if (first) {
        printf("status=timeout");
    }
    putchar('\n');
    fflush(stdout);
}

static void log_status(const char *action, bool has_accepted, bool accepted,
                       bool has_state, int state, bool has_pin)
{
    syslog(LOG_INFO,
           "action=%s result accepted=%s state=%s pin_present=%s",
           action,
           has_accepted ? (accepted ? "true" : "false") : "unknown",
           has_state ? state_name(state) : "unknown",
           has_pin ? "true" : "false");
}

int deneb_df_bridge_main(int argc, char *argv[])
{
    /* Parse arguments */
    const char *action = NULL;
    double timeout_s = 20.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_s = atof(argv[++i]);
        } else if (argv[i][0] != '-') {
            action = argv[i];
        }
    }

    if (!action || (strcmp(action, "connect") != 0 &&
                    strcmp(action, "disconnect") != 0 &&
                    strcmp(action, "status") != 0)) {
        fprintf(stderr, "Usage: deneb-df-bridge <connect|disconnect|status> [--timeout SECS]\n");
        return 1;
    }

    openlog("deneb-df-bridge", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "starting action=%s timeout=%.1f", action, timeout_s);

    /* Create ZMQ context */
    void *ctx = zmq_ctx_new();
    if (!ctx) {
        fprintf(stderr, "status=error reason=zmq-ctx-failed\n");
        syslog(LOG_ERR, "zmq context creation failed");
        closelog();
        return 1;
    }

    /* PUB socket for sending requests */
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    if (!pub) {
        fprintf(stderr, "status=error reason=zmq-pub-failed\n");
        syslog(LOG_ERR, "PUB socket creation failed");
        zmq_ctx_destroy(ctx);
        closelog();
        return 1;
    }
    zmq_setsockopt(pub, ZMQ_LINGER, &(int){0}, sizeof(int));

    char pub_url[64];
    snprintf(pub_url, sizeof(pub_url), "%s%d", IPC_BASE, DENEB_GUI_PUB_PORT);
    if (zmq_bind(pub, pub_url) != 0) {
        if (errno == EADDRINUSE) {
            fprintf(stderr, "status=error reason=stock-menu-slot-in-use\n");
            syslog(LOG_ERR, "GUI pub slot already in use on %s", pub_url);
        } else {
            fprintf(stderr, "status=error reason=bind-failed msg=%s\n",
                    zmq_strerror(errno));
            syslog(LOG_ERR, "bind failed on %s: %s", pub_url,
                   zmq_strerror(errno));
        }
        zmq_close(pub);
        zmq_ctx_destroy(ctx);
        closelog();
        return 1;
    }

    /* SUB socket for reading status (subscribe to all Gershwin pub slots) */
    void *sub = zmq_socket(ctx, ZMQ_SUB);
    if (!sub) {
        fprintf(stderr, "status=error reason=zmq-sub-failed\n");
        syslog(LOG_ERR, "SUB socket creation failed");
        zmq_close(pub);
        zmq_ctx_destroy(ctx);
        closelog();
        return 1;
    }
    zmq_setsockopt(sub, ZMQ_LINGER, &(int){0}, sizeof(int));
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    for (int offset = 0; offset < 4; offset++) {
        char sub_url[64];
        snprintf(sub_url, sizeof(sub_url), "%s%d",
                 IPC_BASE, GERSHWIN_PUB_BASE + offset);
        zmq_connect(sub, sub_url);
    }

    /* ZMQ PUB/SUB drops messages until subscriptions settle */
    struct timespec sleep_ts = {1, 0};
    nanosleep(&sleep_ts, NULL);

    /* Send request if connect or disconnect */
    if (strcmp(action, "connect") == 0 || strcmp(action, "disconnect") == 0) {
        int instr = (strcmp(action, "connect") == 0)
                    ? DF_INSTR_CONNECT : DF_INSTR_DISCONNECT;

        mp_buf_t key_mp, data_mp;
        mp_init(&key_mp);
        mp_init(&data_mp);

        mp_encode_key(&key_mp, monotonic_ms(), "rpc-request", SOURCE,
                      "coordinator/coordinator::digitalfactory::handling@execute|D1");
        mp_encode_df_request(&data_mp, 0, instr);

        zmq_msg_t key_msg, data_msg;
        zmq_msg_init_size(&key_msg, key_mp.len);
        memcpy(zmq_msg_data(&key_msg), key_mp.buf, key_mp.len);
        zmq_msg_init_size(&data_msg, data_mp.len);
        memcpy(zmq_msg_data(&data_msg), data_mp.buf, data_mp.len);

        zmq_msg_send(&key_msg, pub, ZMQ_SNDMORE);
        zmq_msg_send(&data_msg, pub, 0);
        syslog(LOG_INFO, "sent Digital Factory %s request", action);

        zmq_msg_close(&key_msg);
        zmq_msg_close(&data_msg);
        mp_free(&key_mp);
        mp_free(&data_mp);
    }

    /* Read status until deadline */
    int64_t deadline_ms = monotonic_ms() + (int64_t)(timeout_s * 1000.0);

    bool has_accepted = false, accepted = false;
    bool has_state = false;
    int  df_state = DF_STATE_DISCONNECTED;
    bool has_pin = false;
    char *pin = NULL;
    bool done = false;

    while (!done && monotonic_ms() < deadline_ms) {
        int64_t remaining_ms = deadline_ms - monotonic_ms();
        if (remaining_ms <= 0) break;

        zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        int rc = zmq_poll(items, 1, (int)remaining_ms);
        if (rc <= 0) break;

        /* Receive multipart: [key, data] */
        zmq_msg_t key_msg, data_msg;
        zmq_msg_init(&key_msg);
        zmq_msg_init(&data_msg);

        if (zmq_msg_recv(&key_msg, sub, 0) < 0) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            break;
        }

        /* key was sent with SNDMORE, so the data frame follows */
        if (!zmq_msg_more(&key_msg)) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            continue;
        }

        if (zmq_msg_recv(&data_msg, sub, 0) < 0) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            continue;
        }

        /* Decode key */
        mp_reader_t kr;
        mp_reader_init(&kr, zmq_msg_data(&key_msg), zmq_msg_size(&key_msg));

        char *key_action = NULL;
        char *key_target = NULL;

        /* Key is a fixmap with 4 fields */
        uint8_t ktag;
        if (mp_read_u8(&kr, &ktag) >= 0 && (ktag & 0xf0) == 0x80) {
            uint8_t kcount = ktag & 0x0f;
            /* We need action and target */
            for (uint8_t i = 0; i < kcount; i++) {
                char *kname = NULL;
                if (mp_read_str_alloc(&kr, &kname) < 0) break;

                if (strcmp(kname, "action") == 0) {
                    mp_read_str_alloc(&kr, &key_action);
                } else if (strcmp(kname, "target") == 0) {
                    mp_read_str_alloc(&kr, &key_target);
                } else {
                    mp_skip_value(&kr);
                }
                free(kname);
            }
        }

        /* Check if this is a reply to our request */
        bool is_rpc_reply = (key_action && strcmp(key_action, "rpc-reply") == 0);
        char target_buf[256];
        snprintf(target_buf, sizeof(target_buf), "%s/rpc@reply|D1", SOURCE);
        bool is_our_reply = is_rpc_reply &&
                            key_target && strcmp(key_target, target_buf) == 0;

        /* Check if this is a DF status drop */
        bool is_status_drop = (key_action && strcmp(key_action, "drop") == 0);
        bool is_df_status = is_status_drop &&
                            key_target &&
                            strcmp(key_target,
                                  "coordinator::digitalfactory::status") == 0;

        free(key_action);

        /* Decode data */
        mp_reader_t dr;
        mp_reader_init(&dr, zmq_msg_data(&data_msg), zmq_msg_size(&data_msg));

        if (is_our_reply) {
            /* Look for "accepted" field */
            uint8_t dtag;
            if (mp_read_u8(&dr, &dtag) >= 0 && (dtag & 0xf0) == 0x80) {
                uint8_t dcount = dtag & 0x0f;
                if (mp_map_find_key(&dr, dcount, "accepted") == 0) {
                    bool val;
                    if (mp_read_bool(&dr, &val) == 0) {
                        has_accepted = true;
                        accepted = val;
                    }
                }
            }
        } else if (is_df_status) {
            /* Look for state and pin fields */
            uint8_t dtag;
            if (mp_read_u8(&dr, &dtag) >= 0 && (dtag & 0xf0) == 0x80) {
                uint8_t dcount = dtag & 0x0f;

                /* Reset reader to after header for field search */
                size_t data_start = dr.pos;

                /* Look for state */
                dr.pos = data_start;
                if (mp_map_find_key(&dr, dcount, "state") == 0) {
                    int64_t val;
                    if (mp_read_int(&dr, &val) == 0) {
                        has_state = true;
                        df_state = (int)val;
                    }
                }

                /* Look for pin */
                dr.pos = data_start;
                if (mp_map_find_key(&dr, dcount, "pin") == 0) {
                    char *p = NULL;
                    if (mp_read_str_alloc(&dr, &p) == 0) {
                        free(pin);
                        pin = p;
                        has_pin = (pin && pin[0] != '\0');
                    }
                }
            }

            /* Break early if we got a PIN or connected state */
            if (has_pin || df_state == DF_STATE_CONNECTED) {
                done = true;
            }
        }

        free(key_target);
        zmq_msg_close(&key_msg);
        zmq_msg_close(&data_msg);
    }

    /* Output result */
    format_status(has_accepted, accepted, has_state, df_state,
                  has_pin, pin);
    log_status(action, has_accepted, accepted, has_state, df_state, has_pin);

    free(pin);
    zmq_close(sub);
    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    closelog();
    return 0;
}
