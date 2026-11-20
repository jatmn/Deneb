/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_api_main
#include "../src/main.c"
#undef main
#include <assert.h>

const char *backend_zmq_get_status_json(void) { return "{\"connected\":true}"; }

int main(void)
{
    int peers[MAX_SSE_CLIENTS + 1][2];
    epoll_fd = epoll_create1(0);
    assert(epoll_fd >= 0);
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i <= MAX_SSE_CLIENTS; ++i) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peers[i]) == 0);
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = peers[i][0]};
        assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, peers[i][0], &ev) == 0);
        sse_register_client(peers[i][0]);
    }
    assert(sse_client_count == MAX_SSE_CLIENTS);
    /* An over-capacity connection must have no unowned server descriptor. */
    assert(fcntl(peers[MAX_SSE_CLIENTS][0], F_GETFD) == -1 && errno == EBADF);
    close(peers[MAX_SSE_CLIENTS][1]);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        close(peers[i][1]);
    sse_push_status();
    assert(sse_client_count == 0);
    for (int i = 0; i < MAX_SSE_CLIENTS; ++i)
        assert(fcntl(peers[i][0], F_GETFD) == -1 && errno == EBADF);
    /* A client that stops reading cannot retain a slot or a partial event. */
    int slow[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, slow) == 0);
    sse_register_client(slow[0]);
    char fill[4096] = {0};
    while (write(slow[0], fill, sizeof(fill)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
    sse_push_status();
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
