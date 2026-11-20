/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <assert.h>

static void publish_request(void *socket, const char *target)
{
    mp_buf_t key = {0};
    assert(mp_put_u8(&key, 0x82) == 0);
    assert(mp_put_str(&key, "action") == 0);
    assert(mp_put_str(&key, "rpc-request") == 0);
    assert(mp_put_str(&key, "target") == 0);
    /* The Web bridge uses a str8 header for connector service names. */
    size_t target_len = strlen(target);
    assert(target_len <= UINT8_MAX);
    assert(mp_put_u8(&key, 0xd9) == 0);
    assert(mp_put_u8(&key, (uint8_t)target_len) == 0);
    assert(mp_write(&key, target, target_len) == 0);
    assert(zmq_send(socket, key.buf, key.len, ZMQ_SNDMORE) == (int)key.len);
    assert(zmq_send(socket, "", 0, 0) == 0);
    free(key.buf);
}

int main(void)
{
    void *ctx = zmq_ctx_new();
    void *input = zmq_socket(ctx, ZMQ_PAIR);
    void *output = zmq_socket(ctx, ZMQ_PAIR);
    assert(zmq_bind(input, "inproc://deneb-retry") == 0);
    assert(zmq_connect(output, "inproc://deneb-retry") == 0);
    bool want_connect = false;
    bool connect_requested = false;
    publish_request(output, "other::service");
    int64_t start = monotonic_ms();
    wait_for_retry(input, 1, &want_connect, &connect_requested);
    assert(monotonic_ms() - start >= 1000);
    assert(!want_connect && !connect_requested);

    publish_request(output, CONNECT_SERVICE "@execute|D1");
    start = monotonic_ms();
    wait_for_retry(input, 3, &want_connect, &connect_requested);
    assert(monotonic_ms() - start < 2000);
    assert(want_connect && connect_requested);

    start = monotonic_ms();
    wait_for_retry(input, 0, &want_connect, &connect_requested);
    assert(monotonic_ms() - start < 100);
    zmq_close(output);
    zmq_close(input);
    zmq_ctx_term(ctx);
    return 0;
}
