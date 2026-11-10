/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <pthread.h>

static void *wake_retry(void *socket)
{
    usleep(20000);
    assert(zmq_send(socket, "disconnect", 10, 0) == 10);
    zmq_close(socket);
    return NULL;
}
int main(void)
{
    void *ctx = zmq_ctx_new();
    void *input = zmq_socket(ctx, ZMQ_PAIR);
    void *output = zmq_socket(ctx, ZMQ_PAIR);
    assert(zmq_bind(input, "inproc://deneb-retry") == 0);
    assert(zmq_connect(output, "inproc://deneb-retry") == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, wake_retry, output) == 0);
    int64_t start = monotonic_ms();
    wait_for_retry(input, 120);
    assert(monotonic_ms() - start < 2000);
    char message[16];
    assert(zmq_recv(input, message, sizeof(message), ZMQ_DONTWAIT) == 10);
    assert(pthread_join(thread, NULL) == 0);
    start = monotonic_ms();
    wait_for_retry(input, 0);
    assert(monotonic_ms() - start < 100);
    zmq_close(input);
    zmq_ctx_term(ctx);
    return 0;
}
