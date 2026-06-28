/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Digital Factory Gershwin bridge.
 *
 * This code lives in deneb-api so the touchscreen does not embed the Digital
 * Factory connector and Deneb does not ship a second static ZMQ executable.
 */

#include "df_bridge.h"

#ifdef BACKEND_ZMQ_STUB

#include <stdio.h>
#include <string.h>

int deneb_df_bridge_run(const char *action, int timeout_seconds,
                        char *out, size_t out_size)
{
    (void)timeout_seconds;

    if (!action || (strcmp(action, "connect") != 0 &&
                    strcmp(action, "disconnect") != 0 &&
                    strcmp(action, "status") != 0)) {
        snprintf(out, out_size, "status=error reason=bad-action");
        return -1;
    }

    snprintf(out, out_size, "status=stub action=%s", action);
    return 0;
}

#else

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zmq.h>

#if !defined(_WIN32)
#include <syslog.h>
#endif

#define IPC_BASE "tcp://127.0.0.1:"
#define GERSHWIN_PUB_BASE 5546
#define DENEB_GUI_PUB_PORT 5547
#define SOURCE "deneb/df-bridge"
#define DF_STATUS_FILE "/tmp/deneb-df-status"
#define DF_PAIR_REQUEST_FILE "/tmp/deneb-df-pair-request"

#define DF_INSTR_CONNECT 1
#define DF_INSTR_DISCONNECT 2
#define DF_CONNECT_SERVICE "digitalfactory::connector::connect@execute|D1"
#define DF_DISCONNECT_SERVICE "digitalfactory::connector::disconnect@execute|D1"

#define DF_STATE_DISCONNECTED 0
#define DF_STATE_ENTER_PIN 1
#define DF_STATE_CONNECTED 2
#define DF_STATE_RECONNECTING 3

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} mp_buf_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
} mp_reader_t;

static int mp_ensure(mp_buf_t *mp, size_t need)
{
    if (mp->len + need <= mp->cap)
        return 0;
    size_t new_cap = mp->cap ? mp->cap * 2 : 256;
    while (new_cap < mp->len + need)
        new_cap *= 2;
    uint8_t *p = realloc(mp->buf, new_cap);
    if (!p)
        return -1;
    mp->buf = p;
    mp->cap = new_cap;
    return 0;
}

static int mp_write(mp_buf_t *mp, const void *data, size_t len)
{
    if (mp_ensure(mp, len) < 0)
        return -1;
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
    return mp_write(mp, d, sizeof(d));
}

static int mp_put_u32(mp_buf_t *mp, uint32_t v)
{
    uint8_t d[5] = {0xce, (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8), (uint8_t)v};
    return mp_write(mp, d, sizeof(d));
}

static int mp_put_i64(mp_buf_t *mp, int64_t v)
{
    uint8_t d[9] = {0xd3, (uint8_t)(v >> 56), (uint8_t)(v >> 48),
                    (uint8_t)(v >> 40), (uint8_t)(v >> 32),
                    (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8), (uint8_t)v};
    return mp_write(mp, d, sizeof(d));
}

static int mp_put_str(mp_buf_t *mp, const char *s)
{
    size_t len = strlen(s);
    if (len <= 31) {
        if (mp_put_u8(mp, (uint8_t)(0xa0 | len)) < 0)
            return -1;
    } else if (len <= 255) {
        if (mp_put_u8(mp, 0xd9) < 0 || mp_put_u8(mp, (uint8_t)len) < 0)
            return -1;
    } else if (len <= 65535) {
        if (mp_put_u8(mp, 0xda) < 0 || mp_put_u16(mp, (uint16_t)len) < 0)
            return -1;
    } else {
        if (mp_put_u8(mp, 0xdb) < 0 || mp_put_u32(mp, (uint32_t)len) < 0)
            return -1;
    }
    return mp_write(mp, s, len);
}

static int mp_encode_key(mp_buf_t *mp, int64_t ts, const char *action,
                         const char *source, const char *target)
{
    if (mp_put_u8(mp, 0x84) < 0)
        return -1;
    if (mp_put_str(mp, "ts") < 0 || mp_put_i64(mp, ts) < 0)
        return -1;
    if (mp_put_str(mp, "action") < 0 || mp_put_str(mp, action) < 0)
        return -1;
    if (mp_put_str(mp, "source") < 0 || mp_put_str(mp, source) < 0)
        return -1;
    if (mp_put_str(mp, "target") < 0 || mp_put_str(mp, target) < 0)
        return -1;
    return 0;
}

static int mp_encode_df_request(mp_buf_t *mp, int instruction)
{
    if (mp_put_u8(mp, 0x84) < 0)
        return -1;
    if (mp_put_str(mp, "tracker") < 0 || mp_put_u8(mp, 0) < 0)
        return -1;
    if (mp_put_str(mp, "instruction") < 0 ||
        mp_put_u8(mp, (uint8_t)instruction) < 0)
        return -1;
    if (mp_put_str(mp, "data") < 0 || mp_put_u8(mp, 0xc0) < 0)
        return -1;
    if (mp_put_str(mp, "__class__") < 0 ||
        mp_put_str(mp, "DigitalFactoryRequest") < 0)
        return -1;
    return 0;
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int mp_read_u8(mp_reader_t *r, uint8_t *out)
{
    if (r->pos >= r->len)
        return -1;
    *out = r->buf[r->pos++];
    return 0;
}

static int mp_read_bytes(mp_reader_t *r, void *out, size_t n)
{
    if (r->pos + n > r->len)
        return -1;
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return 0;
}

static int mp_skip_value(mp_reader_t *r);

static int mp_skip_values(mp_reader_t *r, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (mp_skip_value(r) < 0)
            return -1;
    }
    return 0;
}

static int mp_skip_value(mp_reader_t *r)
{
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag <= 0x7f || tag >= 0xe0)
        return 0;
    if (tag >= 0xa0 && tag <= 0xbf) {
        r->pos += tag & 0x1f;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag >= 0x80 && tag <= 0x8f)
        return mp_skip_values(r, (tag & 0x0f) * 2);
    if (tag >= 0x90 && tag <= 0x9f)
        return mp_skip_values(r, tag & 0x0f);

    switch (tag) {
    case 0xc0:
    case 0xc2:
    case 0xc3:
        return 0;
    case 0xcc:
    case 0xd0:
        r->pos += 1;
        return r->pos <= r->len ? 0 : -1;
    case 0xcd:
    case 0xd1:
        r->pos += 2;
        return r->pos <= r->len ? 0 : -1;
    case 0xce:
    case 0xd2:
    case 0xca:
        r->pos += 4;
        return r->pos <= r->len ? 0 : -1;
    case 0xcf:
    case 0xd3:
    case 0xcb:
        r->pos += 8;
        return r->pos <= r->len ? 0 : -1;
    case 0xd9: {
        uint8_t len;
        if (mp_read_u8(r, &len) < 0)
            return -1;
        r->pos += len;
        return r->pos <= r->len ? 0 : -1;
    }
    case 0xda:
    case 0xdc:
    case 0xde: {
        uint8_t d[2];
        if (mp_read_bytes(r, d, 2) < 0)
            return -1;
        size_t n = ((size_t)d[0] << 8) | d[1];
        if (tag == 0xda) {
            r->pos += n;
            return r->pos <= r->len ? 0 : -1;
        }
        return mp_skip_values(r, tag == 0xde ? n * 2 : n);
    }
    case 0xdb:
    case 0xdd:
    case 0xdf: {
        uint8_t d[4];
        if (mp_read_bytes(r, d, 4) < 0)
            return -1;
        size_t n = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) |
                   ((size_t)d[2] << 8) | d[3];
        if (tag == 0xdb) {
            r->pos += n;
            return r->pos <= r->len ? 0 : -1;
        }
        return mp_skip_values(r, tag == 0xdf ? n * 2 : n);
    }
    default:
        return -1;
    }
}

static int mp_read_str(mp_reader_t *r, char *out, size_t out_size)
{
    uint8_t tag;
    size_t len = 0;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag >= 0xa0 && tag <= 0xbf) {
        len = tag & 0x1f;
    } else if (tag == 0xd9) {
        uint8_t l;
        if (mp_read_u8(r, &l) < 0)
            return -1;
        len = l;
    } else if (tag == 0xda) {
        uint8_t d[2];
        if (mp_read_bytes(r, d, 2) < 0)
            return -1;
        len = ((size_t)d[0] << 8) | d[1];
    } else {
        return -1;
    }
    if (r->pos + len > r->len || out_size == 0)
        return -1;
    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, r->buf + r->pos, copy);
    out[copy] = '\0';
    r->pos += len;
    return 0;
}

static int mp_read_int(mp_reader_t *r, int64_t *out)
{
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag <= 0x7f) {
        *out = tag;
        return 0;
    }
    if (tag >= 0xe0) {
        *out = (int8_t)tag;
        return 0;
    }
    if (tag == 0xcc || tag == 0xd0) {
        uint8_t v;
        if (mp_read_u8(r, &v) < 0)
            return -1;
        *out = tag == 0xd0 ? (int8_t)v : v;
        return 0;
    }
    if (tag == 0xcd || tag == 0xd1) {
        uint8_t d[2];
        if (mp_read_bytes(r, d, 2) < 0)
            return -1;
        uint16_t u = ((uint16_t)d[0] << 8) | d[1];
        *out = tag == 0xd1 ? (int16_t)u : u;
        return 0;
    }
    if (tag == 0xce || tag == 0xd2) {
        uint8_t d[4];
        if (mp_read_bytes(r, d, 4) < 0)
            return -1;
        uint32_t u = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                     ((uint32_t)d[2] << 8) | d[3];
        *out = tag == 0xd2 ? (int32_t)u : u;
        return 0;
    }
    return -1;
}

static int mp_read_bool(mp_reader_t *r, bool *out)
{
    size_t start = r->pos;
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag == 0xc2 || tag == 0xc3) {
        *out = tag == 0xc3;
        return 0;
    }
    r->pos = start;
    int64_t i;
    if (mp_read_int(r, &i) == 0) {
        *out = i != 0;
        return 0;
    }
    return -1;
}

static const char *state_name(int state)
{
    switch (state) {
    case DF_STATE_DISCONNECTED:
        return "disconnected";
    case DF_STATE_ENTER_PIN:
        return "enter_pin";
    case DF_STATE_CONNECTED:
        return "connected";
    case DF_STATE_RECONNECTING:
        return "reconnecting";
    default:
        return "unknown";
    }
}

static bool digital_factory_is_unpaired_disabled(void)
{
    int has_cluster = system("uci -q get ultimaker.option.cluster_id >/dev/null 2>&1") == 0;
    int enabled = system("/etc/init.d/digitalfactory enabled >/dev/null 2>&1") == 0;
    return !has_cluster && !enabled;
}

static bool read_status_file(char *out, size_t out_size)
{
    FILE *fp = fopen(DF_STATUS_FILE, "r");
    if (!fp)
        return false;
    if (!fgets(out, (int)out_size, fp)) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        out[--len] = '\0';
    return out[0] != '\0';
}

static void write_result(char *out, size_t out_size, bool has_accepted,
                         bool accepted, bool has_state, int state,
                         const char *pin)
{
    size_t used = 0;
    if (out_size == 0)
        return;
    out[0] = '\0';
    if (has_accepted) {
        used += (size_t)snprintf(out + used, out_size - used, "accepted=%d",
                                 accepted ? 1 : 0);
    }
    if (has_state && used < out_size) {
        used += (size_t)snprintf(out + used, out_size - used, "%sstate=%s",
                                 used ? " " : "", state_name(state));
    }
    if (pin && pin[0] && used < out_size) {
        used += (size_t)snprintf(out + used, out_size - used, "%spin=%s",
                                 used ? " " : "", pin);
    }
    if (out[0] == '\0' && read_status_file(out, out_size))
        return;
    if (out[0] == '\0')
        snprintf(out, out_size, "status=timeout");
}

static int read_key_parts(zmq_msg_t *key_msg, char *action, size_t action_size,
                          char *target, size_t target_size)
{
    mp_reader_t r = {
        .buf = (const uint8_t *)zmq_msg_data(key_msg),
        .len = zmq_msg_size(key_msg),
        .pos = 0,
    };
    uint8_t tag;
    if (mp_read_u8(&r, &tag) < 0 || (tag & 0xf0) != 0x80)
        return -1;
    uint8_t count = tag & 0x0f;
    for (uint8_t i = 0; i < count; i++) {
        char name[32];
        if (mp_read_str(&r, name, sizeof(name)) < 0)
            return -1;
        if (strcmp(name, "action") == 0) {
            if (mp_read_str(&r, action, action_size) < 0)
                return -1;
        } else if (strcmp(name, "target") == 0) {
            if (mp_read_str(&r, target, target_size) < 0)
                return -1;
        } else if (mp_skip_value(&r) < 0) {
            return -1;
        }
    }
    return 0;
}

static int read_map_value(zmq_msg_t *msg, const char *key, mp_reader_t *value)
{
    value->buf = (const uint8_t *)zmq_msg_data(msg);
    value->len = zmq_msg_size(msg);
    value->pos = 0;
    uint8_t tag;
    if (mp_read_u8(value, &tag) < 0 || (tag & 0xf0) != 0x80)
        return -1;
    uint8_t count = tag & 0x0f;
    for (uint8_t i = 0; i < count; i++) {
        char name[32];
        if (mp_read_str(value, name, sizeof(name)) < 0)
            return -1;
        if (strcmp(name, key) == 0)
            return 0;
        if (mp_skip_value(value) < 0)
            return -1;
    }
    return -1;
}

static void create_pair_request_file(void)
{
    FILE *fp = fopen(DF_PAIR_REQUEST_FILE, "w");
    if (fp)
        fclose(fp);
}

static void clear_pair_request_file(void)
{
    remove(DF_PAIR_REQUEST_FILE);
}

static int ensure_native_connector_started_for_connect(void)
{
    int enable_rc;
    int start_rc;

    create_pair_request_file();
    enable_rc = system("/etc/init.d/digitalfactory enable >/dev/null 2>&1");
    start_rc = system("/etc/init.d/digitalfactory start >/dev/null 2>&1");
#if !defined(_WIN32)
    syslog(LOG_INFO,
           "digital_factory connect lifecycle enable_rc=%d start_rc=%d",
           enable_rc, start_rc);
#endif
    if (enable_rc == 0 && start_rc == 0)
        return 0;
    clear_pair_request_file();
    return -1;
}

static int send_request(void *pub, const char *action)
{
    int instr = strcmp(action, "connect") == 0 ? DF_INSTR_CONNECT
                                               : DF_INSTR_DISCONNECT;
    const char *target = strcmp(action, "connect") == 0 ? DF_CONNECT_SERVICE
                                                        : DF_DISCONNECT_SERVICE;
    mp_buf_t key = {0};
    mp_buf_t data = {0};
    int rc = -1;

    if (strcmp(action, "connect") == 0)
        create_pair_request_file();

    if (mp_encode_key(&key, monotonic_ms(), "rpc-request", SOURCE, target) < 0)
        goto out;
    if (mp_encode_df_request(&data, instr) < 0)
        goto out;
    if (zmq_send(pub, key.buf, key.len, ZMQ_SNDMORE) < 0)
        goto out;
    if (zmq_send(pub, data.buf, data.len, 0) < 0)
        goto out;
    rc = 0;

out:
    free(key.buf);
    free(data.buf);
    return rc;
}

int deneb_df_bridge_run(const char *action, int timeout_seconds,
                        char *out, size_t out_size)
{
    if (!action || (strcmp(action, "connect") != 0 &&
                    strcmp(action, "disconnect") != 0 &&
                    strcmp(action, "status") != 0)) {
        snprintf(out, out_size, "status=error reason=bad-action");
        return -1;
    }
    if (timeout_seconds <= 0)
        timeout_seconds = 20;
    if (timeout_seconds > 60)
        timeout_seconds = 60;

    if (strcmp(action, "connect") == 0 &&
        ensure_native_connector_started_for_connect() < 0) {
        snprintf(out, out_size,
                 "status=error reason=digitalfactory-start-failed");
        return -1;
    }

    void *ctx = zmq_ctx_new();
    if (!ctx) {
        snprintf(out, out_size, "status=error reason=zmq-ctx-failed");
        return -1;
    }
    (void)zmq_ctx_set(ctx, ZMQ_IO_THREADS, 1);
#ifdef ZMQ_THREAD_STACK_SIZE
    (void)zmq_ctx_set(ctx, ZMQ_THREAD_STACK_SIZE, 64 * 1024);
#endif

    void *pub = zmq_socket(ctx, ZMQ_PUB);
    void *sub = zmq_socket(ctx, ZMQ_SUB);
    if (!pub || !sub) {
        snprintf(out, out_size, "status=error reason=zmq-socket-failed");
        if (pub)
            zmq_close(pub);
        if (sub)
            zmq_close(sub);
        zmq_ctx_destroy(ctx);
        return -1;
    }

    int linger = 0;
    zmq_setsockopt(pub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(sub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    char pub_url[64];
    snprintf(pub_url, sizeof(pub_url), "%s%d", IPC_BASE, DENEB_GUI_PUB_PORT);
    if (zmq_bind(pub, pub_url) != 0) {
        snprintf(out, out_size, "status=error reason=bind-failed msg=%s",
                 zmq_strerror(zmq_errno()));
        zmq_close(sub);
        zmq_close(pub);
        zmq_ctx_destroy(ctx);
        return -1;
    }

    for (int offset = 0; offset < 4; offset++) {
        char sub_url[64];
        snprintf(sub_url, sizeof(sub_url), "%s%d", IPC_BASE,
                 GERSHWIN_PUB_BASE + offset);
        zmq_connect(sub, sub_url);
    }

    struct timespec sleep_ts = {1, 0};
    nanosleep(&sleep_ts, NULL);

    if (strcmp(action, "status") != 0 && send_request(pub, action) < 0) {
        snprintf(out, out_size, "status=error reason=send-failed");
        zmq_close(sub);
        zmq_close(pub);
        zmq_ctx_destroy(ctx);
        return -1;
    }

    bool has_accepted = false;
    bool accepted = false;
    bool has_state = false;
    int state = DF_STATE_DISCONNECTED;
    char pin[64] = "";
    int64_t deadline = monotonic_ms() + (int64_t)timeout_seconds * 1000;

    while (monotonic_ms() < deadline) {
        int64_t remaining = deadline - monotonic_ms();
        zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, (long)remaining) <= 0)
            break;

        zmq_msg_t key_msg;
        zmq_msg_t data_msg;
        zmq_msg_init(&key_msg);
        zmq_msg_init(&data_msg);
        if (zmq_msg_recv(&key_msg, sub, 0) < 0) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            break;
        }
        if (!zmq_msg_more(&key_msg) ||
            zmq_msg_recv(&data_msg, sub, 0) < 0) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            continue;
        }

        char key_action[32] = "";
        char key_target[128] = "";
        if (read_key_parts(&key_msg, key_action, sizeof(key_action),
                           key_target, sizeof(key_target)) == 0) {
            char reply_target[128];
            snprintf(reply_target, sizeof(reply_target), "%s/rpc@reply|D1",
                     SOURCE);
            if (strcmp(key_action, "rpc-reply") == 0 &&
                strcmp(key_target, reply_target) == 0) {
                mp_reader_t val;
                if (read_map_value(&data_msg, "accepted", &val) == 0 &&
                    mp_read_bool(&val, &accepted) == 0) {
                    has_accepted = true;
                }
            } else if (strcmp(key_action, "drop") == 0 &&
                       strcmp(key_target,
                              "coordinator::digitalfactory::status") == 0) {
                mp_reader_t val;
                int64_t i;
                if (read_map_value(&data_msg, "state", &val) == 0 &&
                    mp_read_int(&val, &i) == 0) {
                    has_state = true;
                    state = (int)i;
                }
                if (read_map_value(&data_msg, "pin", &val) == 0)
                    (void)mp_read_str(&val, pin, sizeof(pin));
                if (pin[0] || state == DF_STATE_CONNECTED)
                    deadline = 0;
            }
        }

        zmq_msg_close(&key_msg);
        zmq_msg_close(&data_msg);
    }

    if (strcmp(action, "status") == 0 && !has_state &&
        digital_factory_is_unpaired_disabled()) {
        has_state = true;
        state = DF_STATE_DISCONNECTED;
    }

    write_result(out, out_size, has_accepted, accepted, has_state, state, pin);
#if !defined(_WIN32)
    syslog(LOG_INFO, "digital_factory action=%s result=%s", action, out);
#endif
    zmq_close(sub);
    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}

#endif /* BACKEND_ZMQ_STUB */
