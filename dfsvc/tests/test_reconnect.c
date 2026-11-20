/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <assert.h>

static void publish_request(void *socket, const char *source, const char *target)
{
    mp_buf_t key = {0};
    assert(mp_put_u8(&key, 0x83) == 0);
    assert(mp_put_str(&key, "action") == 0);
    assert(mp_put_str(&key, "rpc-request") == 0);
    assert(mp_put_str(&key, "source") == 0);
    assert(mp_put_str(&key, source) == 0);
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
    int timeout = 1000;
    assert(zmq_setsockopt(output, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    bool want_connect = false;
    bool connect_requested = false;
    const char *bridge = "deneb/df-bridge/123";
    publish_request(output, bridge, "other::service");
    int64_t start = monotonic_ms();
    wait_for_retry(input, input, 1, &want_connect, &connect_requested);
    assert(monotonic_ms() - start >= 1000);
    assert(!want_connect && !connect_requested);
    char data[32];
    assert(zmq_recv(output, data, sizeof(data), ZMQ_DONTWAIT) < 0 && errno == EAGAIN);

    publish_request(output, bridge, CONNECT_SERVICE "@execute|D1");
    start = monotonic_ms();
    wait_for_retry(input, input, 3, &want_connect, &connect_requested);
    assert(monotonic_ms() - start < 2000);
    assert(want_connect && connect_requested);
    zmq_msg_t reply;
    assert(zmq_msg_init(&reply) == 0);
    assert(zmq_msg_recv(&reply, output, 0) > 0 && zmq_msg_more(&reply));
    char action[32] = "", source[128] = "", target[160] = "";
    assert(read_key_parts(&reply, action, sizeof(action), source, sizeof(source),
                          target, sizeof(target)) == 0);
    assert(strcmp(action, "rpc-reply") == 0);
    assert(strcmp(source, SOURCE) == 0);
    assert(strcmp(target, "deneb/df-bridge/123/rpc@reply|D1") == 0);
    zmq_msg_close(&reply);
    assert(zmq_recv(output, data, sizeof(data), 0) == 11);
    assert(memcmp(data, "\x81\xa8" "accepted\xc3", 11) == 0);

    start = monotonic_ms();
    wait_for_retry(input, input, 0, &want_connect, &connect_requested);
    assert(monotonic_ms() - start < 100);
    zmq_close(output);
    zmq_close(input);
    zmq_ctx_term(ctx);
    return 0;
}
