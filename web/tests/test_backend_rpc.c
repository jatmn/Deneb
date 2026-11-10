/* SPDX-License-Identifier: MPL-2.0 */
/* Exercise the actual transport adapters against a local in-process REP peer. */
#ifdef TEST_WEB_BACKEND
#include "../src/backend_zmq.c"
#else
#include "../../ui/src/backend_comm.c"
#endif
#include <assert.h>
#include <pthread.h>

static const char *replies[] = {
    "{\"status\":\"error\",\"message\":\"busy\"}",
    "{\"status\":\"ok\",\"message\":\"accepted\"}",
    "{\"status\":\"ok\"", "", "garbage",
    "{\"status\":\"ok\",\"operation\":\"18446744073709551615\"}"
};
static void *serve(void *socket)
{
    for (size_t i = 0; i < sizeof(replies) / sizeof(replies[0]); ++i) {
        char request[512];
        assert(zmq_recv(socket, request, sizeof(request), 0) > 0);
        assert(zmq_send(socket, replies[i], strlen(replies[i]), 0) >= 0);
    }
    zmq_close(socket);
    return NULL;
}
int main(void)
{
    zmq_ctx = zmq_ctx_new();
    assert(zmq_ctx);
    backend_route.command_url = "inproc://deneb-rpc-regression";
    void *server = zmq_socket(zmq_ctx, ZMQ_REP);
    int timeout = 2000;
    zmq_setsockopt(server, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    assert(zmq_bind(server, backend_route.command_url) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, serve, server) == 0);
#ifdef TEST_WEB_BACKEND
    reset_rpc_socket();
    assert(rpc_sock);
#endif
    const int expected[] = {-1, 0, -1, -1, -1, 0};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
#ifdef TEST_WEB_BACKEND
        int rc = backend_zmq_send_frame("GCODE<[\"M105\"]", strlen("GCODE<[\"M105\"]"));
#else
        int rc = send_rpc_timeout("GCODE<[\"M105\"]", strlen("GCODE<[\"M105\"]"), 2000);
#endif
        assert(rc == expected[i]);
    }
#ifndef TEST_WEB_BACKEND
    assert(backend_get_last_operation() == UINT64_MAX);
#else
    zmq_close(rpc_sock);
#endif
    assert(pthread_join(thread, NULL) == 0);
    zmq_ctx_term(zmq_ctx);
    return 0;
}
