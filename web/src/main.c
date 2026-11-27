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
#include <sys/eventfd.h>
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
#define MAX_SSE_CLIENTS 4
#define SSE_PUSH_INTERVAL_MS 250
/* After a client FIN, lighttpd may need another downstream write to
 * distinguish a half-close from a browser that closed its stream. */
#define SSE_KEEPALIVE_MS 500
#define MAX_PENDING_UPLOADS 8 /* The packaged proxy accepts eight clients. */
#define UPLOAD_IDLE_TIMEOUT_MS 30000
#define UPLOAD_FILE_TEMPLATE DENEB_UPLOAD_TEMP_DIR "/deneb-upload-XXXXXX"

/* The signal handler may run on the upload worker thread. */
_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Signal flag must be lock-free");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Signal wake descriptor must be lock-free");
static atomic_bool running = 1;
static atomic_int wake_fd = -1;
static int listen_fd = -1;
static int epoll_fd = -1;
static int owner_lock_fd = -1;

/* SSE client tracking */
static struct {
    int fd;
    uint64_t revision;
    uint64_t last_write_ms;
} sse_clients[MAX_SSE_CLIENTS];
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

typedef struct {
    char *body;
    size_t body_len;
    size_t body_written;
    size_t header_len;
    size_t header_written;
    int keep_alive;
    uint64_t sse_revision;
} response_write_t;

typedef struct pending_response {
    struct pending_response *next;
    int fd;
    response_write_t output;
    char headers[];
} pending_response_t;

static pending_response_t *pending_responses;

static void wake_event_loop(void)
{
    int fd = atomic_load(&wake_fd);
    if (fd < 0)
        return;
    int saved_errno = errno;
    uint64_t one = 1;
    while (write(fd, &one, sizeof(one)) < 0 && errno == EINTR) {}
    errno = saved_errno;
}

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
    wake_event_loop();
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

enum write_result {
    WRITE_ERROR,
    WRITE_BLOCKED,
    WRITE_COMPLETE
};

static enum write_result write_progress(int fd, const char *data, size_t len, size_t *written)
{
    while (*written < len) {
        ssize_t nw = write(fd, data + *written, len - *written);
        if (nw < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return WRITE_BLOCKED;
            return WRITE_ERROR;
        }
        if (nw == 0) return WRITE_ERROR;
        *written += (size_t)nw;
    }
    return WRITE_COMPLETE;
}

static int write_all(int fd, const char *data, size_t len)
{
    size_t written = 0;
    return write_progress(fd, data, len, &written) == WRITE_COMPLETE ? 0 : -1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int limit_wait(int wait, uint64_t elapsed, int interval)
{
    int remaining = elapsed >= (uint64_t)interval ? 0 : interval - (int)elapsed;
    return wait < 0 || remaining < wait ? remaining : wait;
}

static int next_wait_ms(uint64_t now_ms, int zmq_fd)
{
    int wait = -1;
    if (sse_client_count)
        wait = limit_wait(wait, now_ms - sse_last_push_ms, SSE_PUSH_INTERVAL_MS);
    if (zmq_fd >= 0) {
        const printer_state_t *state = backend_zmq_get_state();
        if (state->connected)
            wait = limit_wait(wait, (uint32_t)now_ms - state->last_update_ms,
                              DENEB_STATUS_STALE_MS);
    }
    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        if (uploads[i].request && !uploads[i].preparation_order)
            wait = limit_wait(wait, now_ms - uploads[i].last_progress_ms,
                              UPLOAD_IDLE_TIMEOUT_MS);
    }
    return wait;
}

static int watch_fd(int fd)
{
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
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
static void sse_register_client(int fd, uint64_t revision)
{
    if (sse_client_count >= MAX_SSE_CLIENTS) {
        fprintf(stderr, "deneb-api: SSE client limit reached\n");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }
    sse_clients[sse_client_count].fd = fd;
    sse_clients[sse_client_count].revision = revision;
    sse_clients[sse_client_count++].last_write_ms = monotonic_ms();
    set_nonblocking(fd);
    fprintf(stderr, "deneb-api: SSE client registered (fd=%d, total=%d)\n", fd, sse_client_count);
}

/* Remove a client from the SSE list */
static void sse_remove_client(int fd)
{
    for (int i = 0; i < sse_client_count; i++) {
        if (sse_clients[i].fd == fd) {
            sse_clients[i] = sse_clients[--sse_client_count];
            fprintf(stderr, "deneb-api: SSE client removed (fd=%d, total=%d)\n", fd, sse_client_count);
            return;
        }
    }
}

/* Push status update to all connected SSE clients */
static void sse_push_status(uint64_t now_ms)
{
    if (sse_client_count == 0 || now_ms - sse_last_push_ms < SSE_PUSH_INTERVAL_MS)
        return;
    sse_last_push_ms = now_ms;

    static const char keepalive[] = ":\n\n";
    size_t event_len;
    uint64_t revision;
    const char *event = backend_zmq_get_status_event(&event_len, &revision);

    for (int i = 0; i < sse_client_count; i++) {
        int fd = sse_clients[i].fd;
        bool changed = event && revision != sse_clients[i].revision;
        if (!changed && now_ms - sse_clients[i].last_write_ms < SSE_KEEPALIVE_MS)
            continue;

        const char *data = changed ? event : keepalive;
        size_t len = changed ? event_len : sizeof(keepalive) - 1;
        if (write(fd, data, len) == (ssize_t)len) {
            if (changed)
                sse_clients[i].revision = revision;
            sse_clients[i].last_write_ms = now_ms;
        } else {
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

static pending_response_t **pending_response_slot(int fd)
{
    pending_response_t **slot = &pending_responses;
    while (*slot && (*slot)->fd != fd)
        slot = &(*slot)->next;
    return slot;
}

static void release_response(pending_response_t **slot)
{
    pending_response_t *pending = *slot;
    if (pending) {
        *slot = pending->next;
        free(pending->output.body);
        free(pending);
    }
}

static void close_client(int fd)
{
    release_response(pending_response_slot(fd));
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

static enum write_result write_response(int fd, const char *headers, response_write_t *output)
{
    enum write_result result = write_progress(fd, headers, output->header_len, &output->header_written);
    if (result != WRITE_COMPLETE)
        return result;
    return write_progress(fd, output->body, output->body_len, &output->body_written);
}

static void finish_response(int fd, uint64_t sse_revision, int keep_alive, enum write_result result)
{
    if (result != WRITE_COMPLETE || (!sse_revision && !keep_alive)) {
        close_client(fd);
        return;
    }

    /* SSE only needs disconnect events. Other persistent sockets read again. */
    struct epoll_event ev = {
        .events = sse_revision ? EPOLLRDHUP | EPOLLHUP | EPOLLERR : EPOLLIN,
        .data.fd = fd,
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
        close_client(fd);
    else if (sse_revision)
        sse_register_client(fd, sse_revision);
}

static int defer_response(int fd, const char *headers, const response_write_t *output)
{
    size_t remaining = output->header_len - output->header_written;
    pending_response_t *pending = malloc(sizeof(*pending) + remaining);
    if (!pending)
        return -1;
    pending->fd = fd;
    pending->output = *output;
    pending->output.header_len = remaining;
    pending->output.header_written = 0;
    memcpy(pending->headers, headers + output->header_written, remaining);

    /* A write-side half-close can still read the pending response. */
    struct epoll_event ev = { .events = EPOLLOUT, .data.fd = fd };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        free(pending);
        return -1;
    }

    pending->next = pending_responses;
    pending_responses = pending;
    return 0;
}

static void resume_response(int fd)
{
    pending_response_t **slot = pending_response_slot(fd);
    pending_response_t *pending = *slot;
    if (!pending)
        return;

    enum write_result result = write_response(fd, pending->headers, &pending->output);
    if (result == WRITE_BLOCKED)
        return;
    uint64_t sse_revision = pending->output.sse_revision;
    int keep_alive = pending->output.keep_alive;
    release_response(slot);
    finish_response(fd, sse_revision, keep_alive, result);
}

static void send_response(int fd, http_response_t *resp)
{
    char headers[768];
    int header_len = api_http_serialize_headers(resp, headers, sizeof(headers));
    enum write_result result = WRITE_ERROR;
    if (header_len > 0) {
        response_write_t output = {
            .body = resp->body,
            .body_len = resp->body_len,
            .header_len = (size_t)header_len,
            .keep_alive = resp->keep_alive,
            .sse_revision = resp->sse_revision,
        };
        result = write_response(fd, headers, &output);
        if (result == WRITE_BLOCKED && defer_response(fd, headers, &output) == 0)
            return;
    }
    free(resp->body);
    finish_response(fd, resp->sse_revision, resp->keep_alive, result);
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
    wake_event_loop();
    return NULL;
}

static bool collect_print_upload(bool status_pending)
{
    upload_t *upload = preparation.upload;
    int fd = upload->client_fd;
    int respond = running && fd >= 0 && !upload_peer_gone(fd);
    if (respond && preparation.prepared) {
        status_pending = backend_zmq_poll();
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
    return status_pending;
}

static void start_print_preparation(void)
{
    while (!preparation.upload && running) {
        upload_t *next = NULL;
        for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
            upload_t *upload = &uploads[i];
            if (upload->request && upload->preparation_order &&
                (!next || upload->preparation_order < next->preparation_order))
                next = upload;
        }
        if (!next)
            return;
        if (upload_peer_gone(next->client_fd)) {
            close_client(next->client_fd);
            continue;
        }
        preparation.upload = next;
        memset(&preparation.files, 0, sizeof(preparation.files));
        atomic_store(&preparation.cancelled, 0);
        api_http_response_init(&preparation.response);
        if (pthread_create(&preparation.thread, NULL, prepare_print_upload, NULL) == 0)
            return;
        set_upload_failed(&preparation.response);
        preparation.prepared = 0;
        (void)collect_print_upload(false);
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

    int result = 0;
    int notify_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    atomic_store(&wake_fd, notify_fd);
    int zmq_fd = backend_zmq_get_fd();
    if (notify_fd < 0 || watch_fd(listen_fd) < 0 || watch_fd(notify_fd) < 0 ||
        (zmq_fd >= 0 && watch_fd(zmq_fd) < 0)) {
        perror("deneb-api: initialize epoll sources");
        result = 1;
        goto shutdown;
    }

    fprintf(stderr, "deneb-api: listening on %s\n", DENEB_API_SOCKET_PATH);

    struct epoll_event events[MAX_EVENTS];
    bool status_pending = backend_zmq_poll();
    while (running) {
        int wait = status_pending ? 0 : next_wait_ms(monotonic_ms(), zmq_fd);
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, wait);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        bool preparation_finished = false;
        for (int i = 0; running && i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                handle_accept();
            } else if (fd == zmq_fd) {
                backend_zmq_poll();
            } else if (fd == notify_fd) {
                uint64_t notifications;
                ssize_t size;
                do {
                    size = read(notify_fd, &notifications, sizeof(notifications));
                } while (size < 0 && errno == EINTR);
                preparation_finished = size == sizeof(notifications);
            } else {
                uint32_t ready = events[i].events;
                upload_t *upload = find_upload(fd);
                if (upload && !upload->preparation_order &&
                    (ready & EPOLLIN) && !(ready & EPOLLERR)) {
                    /* A hangup can accompany the last readable body bytes. */
                    receive_upload(upload);
                } else if (ready & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    close_client(fd);
                } else if (ready & EPOLLOUT) {
                    resume_response(fd);
                } else if (!upload && (ready & EPOLLIN)) {
                    handle_client(fd);
                }
            }
        }

        if (!running)
            break;
        status_pending = backend_zmq_poll();

        uint64_t now_ms = monotonic_ms();
        sse_push_status(now_ms);
        for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
            if (uploads[i].request && !uploads[i].preparation_order &&
                now_ms - uploads[i].last_progress_ms >= UPLOAD_IDLE_TIMEOUT_MS)
                finish_upload(&uploads[i], 0);
        }
        /* Process peer disconnects before publishing a finished upload. */
        if (preparation_finished && preparation.upload) {
            pthread_join(preparation.thread, NULL);
            status_pending = collect_print_upload(status_pending);
        }
        start_print_preparation();
    }

shutdown:
    for (int i = 0; i < MAX_PENDING_UPLOADS; i++) {
        if (uploads[i].request && uploads[i].client_fd >= 0)
            close_client(uploads[i].client_fd);
    }
    if (preparation.upload) {
        pthread_join(preparation.thread, NULL);
        (void)collect_print_upload(false);
    }

    while (pending_responses)
        close_client(pending_responses->fd);

    /* Cleanup SSE clients */
    for (int i = 0; i < sse_client_count; i++) {
        close(sse_clients[i].fd);
    }

    fprintf(stderr, "deneb-api: shutting down\n");
    close(epoll_fd);
    close(listen_fd);
    unlink(DENEB_API_SOCKET_PATH);
    backend_zmq_deinit();
    close(owner_lock_fd);
    if (notify_fd >= 0)
        close(atomic_exchange(&wake_fd, -1));
    return result;
}
