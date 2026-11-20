/* SPDX-License-Identifier: MPL-2.0 */
#include "../src/df_bridge.c"
#include <assert.h>

static void publish(void *pub, const char *action, const char *target,
                    const void *data, size_t size)
{
    mp_buf_t key = {0};
    assert(mp_encode_key(&key, 0, action, "digitalfactory", target) == 0);
    assert(zmq_send(pub, key.buf, key.len, ZMQ_SNDMORE) == (int)key.len);
    assert(zmq_send(pub, data, size, 0) == (int)size);
    free(key.buf);
}

static void test_action_messages(void)
{
    void *ctx = zmq_ctx_new();
    assert(ctx);
    void *pub = zmq_socket(ctx, ZMQ_PAIR);
    void *sub = zmq_socket(ctx, ZMQ_PAIR);
    assert(zmq_bind(pub, "inproc://df-bridge-result") == 0);
    assert(zmq_connect(sub, "inproc://df-bridge-result") == 0);
    const char *reply = "deneb/df-bridge/123/rpc@reply|D1";
    const char *status = "coordinator::digitalfactory::status";
    const char accepted[] = "\x81\xa8" "accepted\xc3";
    const char rejected[] = "\x81\xa8" "accepted\xc2";
    const char disconnected[] = "\x81\xa5" "state\x00";
    const char pin[] = "\x82\xa5" "state\x01\xa3" "pin\xa4" "4321";
    char out[128];

    publish(pub, "rpc-reply", "deneb/df-bridge/122/rpc@reply|D1",
            accepted, sizeof(accepted) - 1);
    publish(pub, "drop", status, disconnected, sizeof(disconnected) - 1);
    publish(pub, "rpc-reply", reply, accepted, sizeof(accepted) - 1);
    assert(wait_for_action(sub, "disconnect", reply, 1, out, sizeof(out)) == -1);
    assert(strcmp(out, "status=error reason=timeout") == 0);

    publish(pub, "rpc-reply", reply, accepted, sizeof(accepted) - 1);
    publish(pub, "drop", status, disconnected, sizeof(disconnected) - 1);
    assert(wait_for_action(sub, "disconnect", reply, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "accepted=1 state=disconnected") == 0);

    publish(pub, "rpc-reply", reply, accepted, sizeof(accepted) - 1);
    publish(pub, "drop", status, pin, sizeof(pin) - 1);
    assert(wait_for_action(sub, "connect", reply, 1, out, sizeof(out)) == 0);
    assert(strcmp(out, "accepted=1 state=enter_pin pin=4321") == 0);

    publish(pub, "rpc-reply", reply, rejected, sizeof(rejected) - 1);
    assert(wait_for_action(sub, "connect", reply, 1, out, sizeof(out)) == -1);
    assert(strcmp(out, "status=error reason=rejected") == 0);

    zmq_close(sub);
    zmq_close(pub);
    zmq_ctx_term(ctx);
}

int main(void)
{
    assert(!bridge_action_complete("connect", false, false, false, 0, ""));
    assert(!bridge_action_complete("connect", false, false, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("connect", true, false, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("connect", true, true, true, DF_STATE_RECONNECTING, ""));
    assert(bridge_action_complete("connect", true, true, true, DF_STATE_ENTER_PIN, "1234"));
    assert(bridge_action_complete("connect", true, true, true, DF_STATE_CONNECTED, ""));
    assert(!bridge_action_complete("disconnect", true, true, true, DF_STATE_CONNECTED, ""));
    assert(bridge_action_complete("disconnect", true, true, true, DF_STATE_DISCONNECTED, ""));
    assert(bridge_action_complete("status", false, false, true, DF_STATE_CONNECTED, ""));
    test_action_messages();
    return 0;
}
