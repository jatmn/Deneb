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
#ifdef TEST_WEB_BACKEND
static void test_status_continues_after_batch(void)
{
    const char *endpoint = "inproc://deneb-status-batch";
    void *pub = zmq_socket(zmq_ctx, ZMQ_XPUB);
    int timeout = 2000;
    assert(pub && zmq_setsockopt(pub, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    assert(zmq_bind(pub, endpoint) == 0);

    status_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    assert(status_sock);
    assert(zmq_setsockopt(status_sock, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC)) == 0);
    assert(zmq_connect(status_sock, endpoint) == 0);
    char frame[128];
    assert(zmq_recv(pub, frame, sizeof(frame), 0) == (int)sizeof(STATUS_TOPIC));

    for (int i = 1; i <= MAX_STATUS_MSGS + 1; i++) {
        int n = snprintf(frame, sizeof(frame), "10001<{\"X\":%d}", i);
        assert(n > 0 && (size_t)n < sizeof(frame) && zmq_send(pub, frame, (size_t)n, 0) == n);
    }
    assert(backend_zmq_poll());
    assert(state.pos_x == MAX_STATUS_MSGS);
    assert(!backend_zmq_poll());
    assert(state.pos_x == MAX_STATUS_MSGS + 1);
    zmq_close(status_sock);
    status_sock = NULL;
    zmq_close(pub);
}

static void publish_until(void *pub, const char *frame, deneb_manual_state_t manual_state)
{
    int n = (int)strlen(frame);
    uint32_t previous_update = state.last_update_ms;
    for (int i = 0; i < 400; ++i) {
        assert(zmq_send(pub, frame, (size_t)n, 0) == n);
        backend_zmq_poll();
        if (state.connected && state.last_update_ms != previous_update &&
            state.manual_id == 9 && state.manual_state == manual_state)
            return;
        struct timespec pause = {0, 5000000};
        nanosleep(&pause, NULL);
    }
    assert(!"status publication was not observed");
}

static void publish_manual_until(void *pub, deneb_manual_state_t manual_state)
{
    char frame[128];
    snprintf(frame, sizeof(frame), "10001<{\"manualId\":\"9\",\"manualState\":%d}",
             (int)manual_state);
    publish_until(pub, frame, manual_state);
}

static uint64_t test_long_status_event(void *pub, uint64_t previous_revision)
{
    char filename[128], firmware[64], machine[16];
    char file_json[256], firmware_json[128], machine_json[32];
    memset(filename, '"', sizeof(filename) - sizeof(".gcode"));
    memcpy(filename + sizeof(filename) - sizeof(".gcode"), ".gcode", sizeof(".gcode"));
    memset(firmware, '"', sizeof(firmware) - 1);
    firmware[sizeof(firmware) - 1] = '\0';
    memset(machine, '"', sizeof(machine) - 1);
    machine[sizeof(machine) - 1] = '\0';
    deneb_json_escape_string(filename, file_json, sizeof(file_json));
    deneb_json_escape_string(firmware, firmware_json, sizeof(firmware_json));
    deneb_json_escape_string(machine, machine_json, sizeof(machine_json));

    char frame[1024];
    int n = snprintf(frame, sizeof(frame),
        "10001<{\"file\":\"%s\",\"firmware\":\"%s\",\"machineType\":\"%s\","
        "\"headTcur\":197.5,\"headTset\":198,\"bedTcur\":63,\"bedTset\":65,"
        "\"topcapTemperature\":45,\"topcapIsPresent\":true,"
        "\"X\":182.5,\"Y\":183.5,\"Z\":184.5,\"Ttot\":2147483647,\"Tleft\":1987654321,"
        "\"pcbId\":1234567890,\"pcbIdValid\":true,\"denebActive\":true,\"denebStopAllowed\":true,"
        "\"manualId\":\"9\",\"manualState\":%d}",
        file_json, firmware_json, machine_json, (int)DENEB_MANUAL_RUNNING);
    assert(n > 0 && (size_t)n < sizeof(frame));
    publish_until(pub, frame, DENEB_MANUAL_RUNNING);

    size_t len;
    uint64_t revision;
    const char *event = backend_zmq_get_status_event(&len, &revision);
    assert(event && revision > previous_revision && len > 1056 && len == strlen(event));
    const char *json = event + strlen("data: ");
    char value[128];
    assert(deneb_json_get_value(json, "filename", value, sizeof(value)) == 0);
    assert(strcmp(value, filename) == 0);
    assert(deneb_json_get_value(json, "firmware", value, sizeof(value)) == 0);
    assert(strcmp(value, firmware) == 0);
    assert(deneb_json_get_value(json, "machine_type", value, sizeof(value)) == 0);
    assert(strcmp(value, machine) == 0);
    return revision;
}

/* SSE and polled status must show the same manual ownership the motion gate
 * enforces, from the real status subscription path. */
static void test_status_projects_manual_ownership(void)
{
    backend_route = deneb_print_backend_route_detect();
    void *pub = zmq_socket(zmq_ctx, ZMQ_PUB);
    assert(pub && zmq_bind(pub, "inproc://deneb-status-regression") == 0);
    status_sock = zmq_socket(zmq_ctx, ZMQ_SUB);
    assert(status_sock);
    configure_status_socket(status_sock);
    assert(zmq_connect(status_sock, "inproc://deneb-status-regression") == 0);
    assert(zmq_setsockopt(status_sock, ZMQ_SUBSCRIBE, STATUS_TOPIC, strlen(STATUS_TOPIC)) == 0);
    const deneb_manual_state_t states[] = {
        DENEB_MANUAL_RUNNING, DENEB_MANUAL_CANCELLING, DENEB_MANUAL_COMPLETE};
    uint64_t revision = 0;
    assert(!status_event);
    publish_manual_until(pub, states[0]);
    assert(!status_event); /* Receiving status without an SSE consumer is lazy. */
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        publish_manual_until(pub, states[i]);
        int allowed = states[i] == DENEB_MANUAL_COMPLETE;
        assert(backend_zmq_manual_action_allowed() == allowed);
        deneb_printer_status_response_t response;
        backend_zmq_get_printer_status_response(&response);
        assert(response.manual_action_allowed == allowed);
        size_t len;
        uint64_t current_revision;
        const char *event = backend_zmq_get_status_event(&len, &current_revision);
        assert(event && len == strlen(event) && current_revision > revision);
        assert(strstr(event, allowed ? "\"manual_action_allowed\":true" :
                                       "\"manual_action_allowed\":false"));
        revision = current_revision;
        publish_manual_until(pub, states[i]);
        assert(backend_zmq_get_status_event(&len, &current_revision) == event);
        assert(current_revision == revision);
    }
    revision = test_long_status_event(pub, revision);
    zmq_close(status_sock);
    status_sock = NULL;
    zmq_close(pub);
    state.last_update_ms -= DENEB_STATUS_STALE_MS;
    backend_zmq_poll();
    size_t len;
    uint64_t expired_revision;
    const char *event = backend_zmq_get_status_event(&len, &expired_revision);
    assert(event && strstr(event, "\"connected\":false") && expired_revision > revision);
}
#endif

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
        int rc = send_rpc("GCODE<[\"M105\"]", strlen("GCODE<[\"M105\"]"));
#endif
        assert(rc == expected[i]);
    }
#ifndef TEST_WEB_BACKEND
    assert(backend_get_last_operation() == UINT64_MAX);
#else
    zmq_close(rpc_sock);
#endif
    assert(pthread_join(thread, NULL) == 0);
#ifdef TEST_WEB_BACKEND
    test_status_projects_manual_ownership();
    test_status_continues_after_batch();
#endif
#ifndef TEST_WEB_BACKEND
    /* Use a bound, silent peer to exercise the real macro admission timeout. */
    backend_route.command_url = "inproc://deneb-rpc-silent";
    server = zmq_socket(zmq_ctx, ZMQ_REP);
    assert(server && zmq_bind(server, backend_route.command_url) == 0);
    struct timespec start, end;
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    assert(backend_send_macro("home") == -1);
    assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    assert(elapsed_ms < 3000);
    zmq_close(server);
#endif
    zmq_ctx_term(zmq_ctx);
    return 0;
}
