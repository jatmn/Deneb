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
#include <poll.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#if !defined(_WIN32)
#include <syslog.h>
#endif

#include "api_http.h"
#include "api_multipart.h"
#include "api_print_job.h"
#include "backend_zmq.h"
#include "df_bridge.h"
#include "print_string.h"

#ifndef DENEB_API_SOCKET_PATH
#define DENEB_API_SOCKET_PATH "/var/run/deneb-api.sock"
#endif
#define MAX_EVENTS      16
#define EPOLL_TIMEOUT_MS 100
#define MAX_SSE_CLIENTS 4
#define SSE_PUSH_INTERVAL_MS 250
#define MAX_PENDING_UPLOADS 8 /* The packaged proxy accepts eight clients. */
#define UPLOAD_IDLE_TIMEOUT_MS 30000
#define UPLOAD_FILE_TEMPLATE DENEB_UPLOAD_TEMP_DIR "/deneb-upload-XXXXXX"

/* The signal handler may run on the upload worker thread. */
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Signal flag must be lock-free");
static atomic_bool running = 1;
static int listen_fd = -1;
static int epoll_fd = -1;
static int owner_lock_fd = -1;

/* SSE client tracking */
static int sse_clients[MAX_SSE_CLIENTS];
static int sse_client_count = 0;
static uint64_t sse_last_push_ms = 0;

typedef struct {
    char path[HTTP_MAX_PATH];
    char authorization[HTTP_HEADER_VALUE_SIZE];
    char upload_path[sizeof(UPLOAD_FILE_TEMPLATE)];
    char multipart_boundary[HTTP_MULTIPART_BOUNDARY_SIZE];
    int content_length;
    int upload_size;
    int body_prefix_len;
} upload_request_t;

_Static_assert(sizeof(UPLOAD_FILE_TEMPLATE) <= sizeof(((http_request_t *)0)->upload_path),
               "Upload name must fit in the routed request");

typedef struct {
    int client_fd;
    int temp_fd;
    uint64_t last_progress_ms;
    uint64_t preparation_order;
    upload_request_t *request;
} upload_t;

static upload_t uploads[MAX_PENDING_UPLOADS];
static uint64_t next_preparation_order;
static struct {
    upload_t *upload;
    pthread_t thread;
    atomic_bool cancelled;
    int prepared;
    api_print_upload_t files;
    http_response_t response;
} preparation;

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

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int create_listen_socket_at(const char *path)
{
    char lock_path[sizeof(((struct sockaddr_un *)0)->sun_path) + 8];
    int length = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    if (length < 0 || (size_t)length >= sizeof(lock_path))
        return -1;
    int lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0)
        return -1;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(lock_fd);
        return -1;
    }
    owner_lock_fd = lock_fd;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("socket"); return -1; }

    unlink(path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

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

    chmod(path, 0660);
    set_nonblocking(fd);
    return fd;
}

/* Register a file descriptor as an SSE client */
void sse_register_client(int fd)
{
    if (sse_client_count >= MAX_SSE_CLIENTS) {
        fprintf(stderr, "deneb-api: SSE client limit reached\n");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
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
        if (nw != n) {
            /* A slow reader must reconnect rather than receive a truncated
             * event or retain a slot indefinitely. No per-client buffers. */
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            sse_remove_client(fd);
            i--; /* re-check this index */
        }
    }
}

static void handle_accept(void)
{
    int client_fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept");
        return;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        perror("epoll_ctl add client");
        close(client_fd);
    }
}

static upload_t *find_upload(int fd)
{
    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        if (uploads[i].request && uploads[i].client_fd == fd)
            return &uploads[i];
    }
    return NULL;
}

static void release_upload(upload_t *upload)
{
    if (upload->temp_fd >= 0)
        close(upload->temp_fd);
    unlink(upload->request->upload_path);
    free(upload->request);
    upload->request = NULL;
}

static void close_client(int fd)
{
    upload_t *upload = find_upload(fd);
    if (upload && upload == preparation.upload) {
        upload->client_fd = -1;
        atomic_store(&preparation.cancelled, 1);
    } else if (upload) {
        release_upload(upload);
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    sse_remove_client(fd);
    close(fd);
}

static void send_response(int fd, http_response_t *resp)
{
    char out_buf[65536];
    int out_len = api_http_serialize(resp, out_buf, sizeof(out_buf));
    free(resp->body);

    if (out_len <= 0 || write_all(fd, out_buf, (size_t)out_len) < 0) {
        close_client(fd);
    } else if (resp->is_sse) {
        /* Re-register fd to only detect disconnection, not incoming data. */
        struct epoll_event sev = {
            .events = EPOLLRDHUP | EPOLLHUP | EPOLLERR,
            .data.fd = fd,
        };
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &sev) == 0)
            sse_register_client(fd);
        else
            close_client(fd);
    } else if (!resp->keep_alive) {
        close_client(fd);
    }
}

static void set_upload_failed(http_response_t *resp)
{
    resp->status_code = 500;
    api_http_set_body_str(resp, "{\"message\":\"Upload failed\"}");
}

static int upload_peer_gone(int fd)
{
    /* A write-side half-close still allows the upstream to read our reply. */
    struct pollfd peer = { .fd = fd };
    return poll(&peer, 1, 0) > 0 && (peer.revents & (POLLHUP | POLLERR | POLLNVAL));
}

static void *prepare_print_upload(void *unused)
{
    (void)unused;
    const upload_request_t *request = preparation.upload->request;
    preparation.prepared = api_print_job_prepare_upload(
        request->multipart_boundary, request->upload_path,
        &preparation.files, &preparation.response,
        &preparation.cancelled) == 0;
    return NULL;
}

static void collect_print_upload(void)
{
    upload_t *upload = preparation.upload;
    int fd = upload->client_fd;
    int respond = running && fd >= 0 && !upload_peer_gone(fd);
    if (respond && preparation.prepared) {
        backend_zmq_poll();
        api_print_job_commit_upload(&preparation.files, &preparation.response);
    }
    api_print_job_release_upload(&preparation.files);
    release_upload(upload);
    preparation.upload = NULL;
    if (respond) {
        send_response(fd, &preparation.response);
    } else {
        free(preparation.response.body);
        if (fd >= 0)
            close_client(fd);
    }
}

static void poll_print_uploads(void)
{
    if (preparation.upload) {
        /* A successful join publishes the results and never waits for disk work. */
        if (pthread_tryjoin_np(preparation.thread, NULL) != 0)
            return;
        collect_print_upload();
    }

    upload_t *next = NULL;
    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        upload_t *upload = &uploads[i];
        if (upload->request && upload->preparation_order &&
            (!next || upload->preparation_order < next->preparation_order))
            next = upload;
    }
    if (!next || !running)
        return;
    if (upload_peer_gone(next->client_fd)) {
        close_client(next->client_fd);
        return;
    }

    preparation.upload = next;
    memset(&preparation.files, 0, sizeof(preparation.files));
    atomic_store(&preparation.cancelled, 0);
    api_http_response_init(&preparation.response);
    if (pthread_create(&preparation.thread, NULL, prepare_print_upload, NULL) != 0) {
        set_upload_failed(&preparation.response);
        preparation.prepared = 0;
        collect_print_upload();
    }
}

static int route_completed_upload(upload_t *upload, http_response_t *resp)
{
    const upload_request_t *saved = upload->request;
    http_request_t req = { .method = "POST", .is_multipart = 1 };
    snprintf(req.path, sizeof(req.path), "%s", saved->path);
    snprintf(req.upload_path, sizeof(req.upload_path), "%s", saved->upload_path);
    snprintf(req.multipart_boundary, sizeof(req.multipart_boundary), "%s",
             saved->multipart_boundary);
    req.content_length = saved->content_length;
    req.upload_size = saved->upload_size;
    if (saved->authorization[0]) {
        memcpy(req.headers[0].name, "Authorization", sizeof("Authorization"));
        snprintf(req.headers[0].value, sizeof(req.headers[0].value), "%s",
                 saved->authorization);
        req.header_count = 1;
    }

    /* Preserve the initial body prefix for POST handlers that read req.body. */
    while (req.body_len < saved->body_prefix_len) {
        ssize_t n = pread(upload->temp_fd, req.body + req.body_len,
                          (size_t)(saved->body_prefix_len - req.body_len), req.body_len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        req.body_len += (int)n;
    }

    int closed = close(upload->temp_fd);
    upload->temp_fd = -1;
    if (closed < 0)
        return -1;

    fprintf(stderr, "deneb-api: uploaded %d bytes to %s\n",
            saved->upload_size, saved->upload_path);
    backend_zmq_poll();
    api_http_route(&req, resp);
    return 0;
}

static void finish_upload(upload_t *upload, int complete)
{
    int fd = upload->client_fd;
    http_response_t resp;
    api_http_response_init(&resp);

    if (complete && route_completed_upload(upload, &resp) == 0) {
        if (resp.prepare_print_upload) {
            /* Only receive full disconnects while waiting to send the reply. */
            struct epoll_event ev = { .data.fd = fd };
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
                set_upload_failed(&resp);
            } else {
                upload->preparation_order = ++next_preparation_order;
                return;
            }
        }
    } else {
        set_upload_failed(&resp);
    }
    release_upload(upload);
    send_response(fd, &resp);
}

static size_t upload_remaining(const upload_t *upload)
{
    return (size_t)(upload->request->content_length - upload->request->upload_size);
}

static void append_upload(upload_t *upload, const char *data, size_t len)
{
    size_t remaining = upload_remaining(upload);
    if (len > remaining)
        len = remaining;
    if (write_all(upload->temp_fd, data, len) < 0) {
        finish_upload(upload, 0);
        return;
    }
    upload->request->upload_size += (int)len;
    upload->last_progress_ms = monotonic_ms();
    if (len == remaining)
        finish_upload(upload, 1);
}

static int start_upload(int fd, const http_request_t *req, const char *data, size_t len)
{
    upload_t *upload = NULL;
    /* The receive files share /tmp, which may be RAM-backed on the printer. */
    int available = HTTP_MAX_CONTENT_LENGTH;
    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        if (uploads[i].request)
            available -= uploads[i].request->content_length;
        else if (!upload)
            upload = &uploads[i];
    }
    if (!upload || req->content_length > available)
        return -1;

    upload_request_t *saved = malloc(sizeof(*saved));
    if (!saved)
        return -1;
    snprintf(saved->path, sizeof(saved->path), "%s", req->path);
    const char *authorization = api_http_authorization(req);
    snprintf(saved->authorization, sizeof(saved->authorization), "%s",
             authorization ? authorization : "");
    snprintf(saved->multipart_boundary, sizeof(saved->multipart_boundary), "%s",
             req->multipart_boundary);
    memcpy(saved->upload_path, UPLOAD_FILE_TEMPLATE, sizeof(saved->upload_path));
    saved->content_length = req->content_length;
    saved->upload_size = 0;
    saved->body_prefix_len = req->body_len < req->content_length ?
        req->body_len : req->content_length;
    int temp_fd = mkostemp(saved->upload_path, O_CLOEXEC);
    if (temp_fd < 0) {
        perror("mkostemp");
        free(saved);
        return -1;
    }
    *upload = (upload_t){ .client_fd = fd, .temp_fd = temp_fd, .request = saved };
    append_upload(upload, data, len);
    return 0;
}

static void receive_upload(upload_t *upload)
{
    /* One chunk per readiness event lets other clients and SSE make progress. */
    char chunk[8192];
    size_t remaining = upload_remaining(upload);
    ssize_t n;
    do {
        n = read(upload->client_fd, chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk));
    } while (n < 0 && errno == EINTR);
    if (n > 0) {
        append_upload(upload, chunk, (size_t)n);
    } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        finish_upload(upload, 0);
    }
}

static void handle_client(int fd)
{
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        close_client(fd);
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
        const char *body_start = strstr(buf, "\r\n\r\n");
        const char *body = body_start ? body_start + 4 : buf + n;
        if (start_upload(fd, &req, body, (size_t)(buf + n - body)) == 0)
            return;
        set_upload_failed(&resp);
    } else {
        backend_zmq_poll();
        api_http_route(&req, &resp);
    }
    send_response(fd, &resp);
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
            if (strcmp(argv[i], "--timeout") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "deneb-api: invalid --timeout value\n");
                    return 1;
                }
                if (deneb_parse_int(argv[++i], &timeout) != 0) {
                    fprintf(stderr, "deneb-api: invalid --timeout value\n");
                    return 1;
                }
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

    listen_fd = create_listen_socket_at(DENEB_API_SOCKET_PATH);
    if (listen_fd < 0) {
        fprintf(stderr, "deneb-api: failed to create listen socket\n");
        backend_zmq_deinit();
        return 1;
    }

    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        unlink(DENEB_API_SOCKET_PATH);
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

    fprintf(stderr, "deneb-api: listening on %s\n", DENEB_API_SOCKET_PATH);

    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; running && i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                handle_accept();
            } else if (fd == zmq_fd) {
                backend_zmq_poll();
            } else {
                uint32_t ready = events[i].events;
                upload_t *upload = find_upload(fd);
                if (upload && !upload->preparation_order &&
                    (ready & EPOLLIN) && !(ready & EPOLLERR)) {
                    /* A hangup can accompany the last readable body bytes. */
                    receive_upload(upload);
                } else if (ready & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    close_client(fd);
                } else if (!upload && (ready & EPOLLIN)) {
                    handle_client(fd);
                }
            }
        }

        backend_zmq_poll();

        /* Push SSE updates at 250 ms cadence */
        uint64_t now_ms = monotonic_ms();
        if (sse_client_count > 0 && (sse_last_push_ms == 0 || now_ms - sse_last_push_ms >= SSE_PUSH_INTERVAL_MS)) {
            sse_push_status();
            sse_last_push_ms = now_ms;
        }
        for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
            if (uploads[i].request && !uploads[i].preparation_order &&
                now_ms - uploads[i].last_progress_ms >= UPLOAD_IDLE_TIMEOUT_MS)
                finish_upload(&uploads[i], 0);
        }
        poll_print_uploads();
    }

    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        if (uploads[i].request && uploads[i].client_fd >= 0)
            close_client(uploads[i].client_fd);
    }
    if (preparation.upload) {
        pthread_join(preparation.thread, NULL);
        collect_print_upload();
    }

    /* Cleanup SSE clients */
    for (int i = 0; i < sse_client_count; i++) {
        close(sse_clients[i]);
    }

    fprintf(stderr, "deneb-api: shutting down\n");
    close(epoll_fd);
    close(listen_fd);
    unlink(DENEB_API_SOCKET_PATH);
    backend_zmq_deinit();
    close(owner_lock_fd);
    return 0;
}
