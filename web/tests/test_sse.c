/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_api_main
#include "../src/main.c"
#undef main
#include <assert.h>

static char status_event[1200];
static uint64_t status_revision = 1;
static int snapshot_requests;
static printer_state_t printer_state;

const printer_state_t *backend_zmq_get_state(void)
{
    return &printer_state;
}

int api_print_job_prepare_upload(const char *boundary, const char *upload_path,
                                 api_print_upload_t *upload, http_response_t *resp,
                                 const atomic_bool *cancelled)
{
    (void)upload;
    (void)resp;
    assert(strcmp(boundary, "status-fixture") == 0);
    assert(strcmp(upload_path, UPLOAD_FILE_TEMPLATE) == 0);
    assert(!atomic_load(cancelled));
    return 0;
}

static void test_event_wait(void)
{
    const uint64_t now = (uint64_t)UINT32_MAX + 1;
    assert(next_wait_ms(now, -1) == -1);

    printer_state.connected = true;
    printer_state.last_update_ms = (uint32_t)(now - 100);
    assert(next_wait_ms(now, 1) == DENEB_STATUS_STALE_MS - 100);
    upload_request_t request = { .multipart_boundary = "status-fixture",
                                 .upload_path = UPLOAD_FILE_TEMPLATE };
    uploads[0].request = &request;
    uploads[0].last_progress_ms = now - UPLOAD_IDLE_TIMEOUT_MS + 350;
    assert(next_wait_ms(now, 1) == 350);
    sse_client_count = 1;
    sse_last_push_ms = now - 50;
    assert(next_wait_ms(now, 1) == SSE_PUSH_INTERVAL_MS - 50);
    sse_client_count = 0;
    assert(next_wait_ms(now + 350, 1) == 0);

    uploads[0].preparation_order = 1;
    assert(next_wait_ms(now, -1) == -1);
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    assert(fd >= 0 && watch_fd(fd) == 0);
    atomic_store(&wake_fd, fd);
    preparation.upload = &uploads[0];
    assert(pthread_create(&preparation.thread, NULL, prepare_print_upload, NULL) == 0);
    struct epoll_event ready;
    assert(epoll_wait(epoll_fd, &ready, 1, 2000) == 1 && ready.data.fd == fd);
    uint64_t notification;
    assert(read(fd, &notification, sizeof(notification)) == sizeof(notification));
    assert(pthread_join(preparation.thread, NULL) == 0 && preparation.prepared);

    signal_handler(SIGTERM);
    assert(!running && epoll_wait(epoll_fd, &ready, 1, 0) == 1 && ready.data.fd == fd);
    assert(close(atomic_exchange(&wake_fd, -1)) == 0);
    running = true;
    preparation.upload = NULL;
    uploads[0] = (upload_t){0};
    printer_state = (printer_state_t){0};
    sse_last_push_ms = 0;
}

const char *backend_zmq_get_status_event(size_t *len, uint64_t *revision)
{
    snapshot_requests++;
    *len = strlen(status_event);
    *revision = status_revision;
    return status_event;
}

static void assert_event(int fd, const char *expected)
{
    char received[sizeof(status_event)];
    size_t length = strlen(expected);
    assert(recv(fd, received, length, MSG_WAITALL | MSG_DONTWAIT) == (ssize_t)length);
    assert(memcmp(received, expected, length) == 0);
}

static void assert_quiet(int fd)
{
    char byte;
    assert(recv(fd, &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);
}

int main(void)
{
    int peers[MAX_SSE_CLIENTS + 1][2];
    epoll_fd = epoll_create1(0);
    assert(epoll_fd >= 0);
    signal(SIGPIPE, SIG_IGN);
    test_event_wait();
    uint64_t now = monotonic_ms();
    sse_push_status(now);
    assert(snapshot_requests == 0);
    assert(snprintf(status_event, sizeof(status_event), "data: {\"filename\":\"%1100s\"}\n\n", "changed") > 1056);

    for (int i = 0; i <= MAX_SSE_CLIENTS; ++i) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peers[i]) == 0);
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = peers[i][0]};
        assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, peers[i][0], &ev) == 0);
        sse_register_client(peers[i][0], i ? status_revision : status_revision - 1);
    }
    assert(sse_client_count == MAX_SSE_CLIENTS);
    /* An over-capacity connection must have no unowned server descriptor. */
    assert(fcntl(peers[MAX_SSE_CLIENTS][0], F_GETFD) == -1 && errno == EBADF);
    close(peers[MAX_SSE_CLIENTS][1]);

    now = monotonic_ms();
    sse_push_status(now);
    assert(snapshot_requests == 1);
    assert_event(peers[0][1], status_event);
    for (int i = 1; i < MAX_SSE_CLIENTS; ++i)
        assert_quiet(peers[i][1]); /* These peers already received the initial revision. */

    ++status_revision;
    sse_push_status(now + SSE_PUSH_INTERVAL_MS - 1);
    assert(snapshot_requests == 1);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert_quiet(peers[i][1]);

    now += SSE_PUSH_INTERVAL_MS;
    sse_push_status(now);
    assert(snapshot_requests == 2);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert_event(peers[i][1], status_event);

    now += SSE_PUSH_INTERVAL_MS;
    sse_push_status(now);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert_quiet(peers[i][1]);
    now += SSE_KEEPALIVE_MS;
    sse_push_status(now);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert_event(peers[i][1], ":\n\n");

    ++status_revision;
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        close(peers[i][1]);
    now += SSE_PUSH_INTERVAL_MS;
    sse_push_status(now);
    assert(sse_client_count == 0);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert(fcntl(peers[i][0], F_GETFD) == -1 && errno == EBADF);
    /* A client that stops reading cannot retain a slot or a partial event. */
    int slow[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, slow) == 0);
    sse_register_client(slow[0], status_revision);
    char fill[4096] = {0};
    while (write(slow[0], fill, sizeof(fill)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    ++status_revision;
    sse_push_status(now + SSE_PUSH_INTERVAL_MS);
    assert(sse_client_count == 0);
    assert(fcntl(slow[0], F_GETFD) == -1 && errno == EBADF);
    close(slow[1]);

    char path[108], lock[116];
    snprintf(path, sizeof(path), "%s/api-%ld.sock", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    snprintf(lock, sizeof(lock), "%s.lock", path);
    int owner = create_listen_socket_at(path);
    assert(owner >= 0);
    assert(create_listen_socket_at(path) < 0);
    struct stat st;
    assert(stat(path, &st) == 0 && S_ISSOCK(st.st_mode));
    close(owner);
    close(owner_lock_fd);
    unlink(path);
    unlink(lock);
    close(epoll_fd);
    return 0;
}
