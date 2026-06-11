/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Deneb API Server - main entry point.
 * Implements UltiMaker REST API v1 over Unix domain socket.
 */

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <poll.h>
#if !defined(_WIN32)
#include <syslog.h>
#endif

#include "api_http.h"
#include "backend_zmq.h"
#include "df_bridge.h"

#define SOCKET_PATH     "/var/run/deneb-api.sock"
#define MAX_EVENTS      16
#define EPOLL_TIMEOUT_MS 100
#define MAX_SSE_CLIENTS 4
#define SSE_PUSH_INTERVAL_MS 250

static volatile int running = 1;
static int listen_fd = -1;
static int epoll_fd = -1;

/* SSE client tracking */
static int sse_clients[MAX_SSE_CLIENTS];
static int sse_client_count = 0;
static uint64_t sse_last_push_ms = 0;

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int write_all(int fd, const char *data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t nw = write(fd, data + written, len - written);
        if (nw < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nw == 0) return -1;
        written += (size_t)nw;
    }
    return 0;
}

static int create_listen_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    chmod(SOCKET_PATH, 0660);
    set_nonblocking(fd);
    return fd;
}

/* Register a file descriptor as an SSE client */
void sse_register_client(int fd)
{
    if (sse_client_count >= MAX_SSE_CLIENTS) {
        fprintf(stderr, "deneb-api: SSE client limit reached\n");
        return;
    }
    sse_clients[sse_client_count++] = fd;
    set_nonblocking(fd);
    fprintf(stderr, "deneb-api: SSE client registered (fd=%d, total=%d)\n", fd, sse_client_count);
}

/* Remove a client from the SSE list */
static void sse_remove_client(int fd)
{
    for (int i = 0; i < sse_client_count; i++) {
        if (sse_clients[i] == fd) {
            sse_clients[i] = sse_clients[--sse_client_count];
            fprintf(stderr, "deneb-api: SSE client removed (fd=%d, total=%d)\n", fd, sse_client_count);
            return;
        }
    }
}

/* Push status update to all connected SSE clients */
static void sse_push_status(void)
{
    if (sse_client_count == 0) return;

    const char *json = backend_zmq_get_status_json();
    if (!json) return;

    char event[1056];
    int n = snprintf(event, sizeof(event), "data: %s\n\n", json);
    if (n <= 0 || n >= (int)sizeof(event)) return;

    for (int i = 0; i < sse_client_count; i++) {
        int fd = sse_clients[i];
        ssize_t nw = write(fd, event, (size_t)n);
        if (nw < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            /* Client disconnected */
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            sse_remove_client(fd);
            i--; /* re-check this index */
        }
    }
}

static void handle_accept(void)
{
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept");
        return;
    }
    set_nonblocking(client_fd);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        perror("epoll_ctl add client");
        close(client_fd);
    }
}

/* Stream multipart POST body to a temp file. Returns 0 on success. */
static int stream_upload(int fd, http_request_t *req, const char *initial_body, int initial_len)
{
    char path[128];
    snprintf(path, sizeof(path), "/tmp/deneb-upload-XXXXXX");
    int tmpfd = mkstemp(path);
    if (tmpfd < 0) {
        perror("mkstemp");
        return -1;
    }

    int remaining = req->content_length;
    int written = 0;

    /* Write initial body data already read */
    if (initial_len > 0 && remaining > 0) {
        int to_write = initial_len < remaining ? initial_len : remaining;
        if (write_all(tmpfd, initial_body, (size_t)to_write) < 0) {
            close(tmpfd); unlink(path);
            return -1;
        }
        written += to_write;
        remaining -= to_write;
    }

    /* Stream rest of body in 8KB chunks (bounded stack usage) */
    char chunk[8192];
    while (remaining > 0) {
        ssize_t to_read = (ssize_t)(remaining < (int)sizeof(chunk) ? remaining : (int)sizeof(chunk));
        ssize_t nr = read(fd, chunk, (size_t)to_read);
        if (nr <= 0) {
            if (nr < 0 && errno == EINTR) continue;
            if (nr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLIN;
                pfd.revents = 0;
                int pr = poll(&pfd, 1, 30000);
                if (pr > 0) continue;
            }
            close(tmpfd); unlink(path);
            return -1;
        }
        if (write_all(tmpfd, chunk, (size_t)nr) < 0) {
            close(tmpfd); unlink(path);
            return -1;
        }
        written += (int)nr;
        remaining -= (int)nr;
    }

    if (remaining != 0) {
        close(tmpfd); unlink(path);
        return -1;
    }

    close(tmpfd);
    snprintf(req->upload_path, sizeof(req->upload_path), "%s", path);
    req->upload_size = written;
    fprintf(stderr, "deneb-api: uploaded %d bytes to %s\n", written, path);
    return 0;
}

static void handle_client(int fd)
{
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        sse_remove_client(fd);
        return;
    }
    buf[n] = '\0';

    http_request_t req;
    http_response_t resp;
    api_http_response_init(&resp);

    if (api_http_parse(&req, buf, (size_t)n) < 0) {
        resp.status_code = 400;
        api_http_set_body_str(&resp, "{\"message\":\"Bad request\"}");
    } else if (req.is_multipart && strcmp(req.method, "POST") == 0) {
        /* Stream multipart body to temp file for upload handlers. */
        /* Find where body starts in the initial read */
        const char *body_start = strstr(buf, "\r\n\r\n");
        int body_offset = body_start ? (int)(body_start + 4 - buf) : (int)n;
        int initial_body_len = (int)n - body_offset;

        if (stream_upload(fd, &req, buf + body_offset, initial_body_len) < 0) {
            resp.status_code = 500;
            api_http_set_body_str(&resp, "{\"message\":\"Upload failed\"}");
        } else {
            backend_zmq_poll();
            api_http_route(&req, &resp);
        }
        /* Cleanup temp upload file */
        if (req.upload_path[0]) unlink(req.upload_path);
    } else {
        backend_zmq_poll();
        api_http_route(&req, &resp);
    }

    char out_buf[65536];
    int out_len = api_http_serialize(&resp, out_buf, sizeof(out_buf));

    /* Write response with partial-write handling */
    int total_written = 0;
    while (total_written < out_len) {
        ssize_t nw = write(fd, out_buf + total_written, (size_t)(out_len - total_written));
        if (nw < 0) {
            if (errno == EINTR) continue;
            break;
        }
        total_written += (int)nw;
    }

    if (resp.body) free(resp.body);

    if (resp.is_sse) {
        /* Re-register fd to only detect disconnection, not incoming data */
        struct epoll_event sev;
        sev.events = EPOLLHUP | EPOLLERR;
        sev.data.fd = fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &sev);
        sse_register_client(fd);
    } else if (!resp.keep_alive) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
    }
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "digital-factory") == 0) {
        if (argc < 3) {
            fprintf(stderr,
                    "Usage: %s digital-factory <connect|disconnect|status> "
                    "[--timeout SECS]\n",
                    argv[0]);
            return 1;
        }
        int timeout = 20;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
                timeout = atoi(argv[++i]);
            }
        }
        char result[160];
        int rc = deneb_df_bridge_run(argv[2], timeout, result, sizeof(result));
        printf("%s\n", result);
        return rc == 0 ? 0 : 1;
    }

#if !defined(_WIN32)
    openlog("deneb-api", LOG_PID, LOG_DAEMON);
#endif

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "deneb-api: starting (version=%s)\n", DENEB_VERSION);

    api_http_init();

    if (backend_zmq_init() < 0) {
        fprintf(stderr, "deneb-api: failed to init ZMQ backend\n");
        return 1;
    }

    listen_fd = create_listen_socket();
    if (listen_fd < 0) {
        fprintf(stderr, "deneb-api: failed to create listen socket\n");
        backend_zmq_deinit();
        return 1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        unlink(SOCKET_PATH);
        backend_zmq_deinit();
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    int zmq_fd = backend_zmq_get_fd();
    if (zmq_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = zmq_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, zmq_fd, &ev);
    }

    fprintf(stderr, "deneb-api: listening on %s\n", SOCKET_PATH);

    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                handle_accept();
            } else if (fd == zmq_fd) {
                backend_zmq_poll();
            } else if (events[i].events & EPOLLIN) {
                handle_client(fd);
            }
        }

        /* Push SSE updates at 250 ms cadence */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t now_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
        if (sse_client_count > 0 && (sse_last_push_ms == 0 || now_ms - sse_last_push_ms >= SSE_PUSH_INTERVAL_MS)) {
            sse_push_status();
            sse_last_push_ms = now_ms;
        }
    }

    /* Cleanup SSE clients */
    for (int i = 0; i < sse_client_count; i++) {
        close(sse_clients[i]);
    }

    fprintf(stderr, "deneb-api: shutting down\n");
    close(epoll_fd);
    close(listen_fd);
    unlink(SOCKET_PATH);
    backend_zmq_deinit();
    return 0;
}
