/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "backend_zmq.h"

int __real_accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);

int __wrap_accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    int client = __real_accept4(fd, addr, addrlen, flags);
    /* Only the explicitly armed slow-reader connection gets a small queue. */
    if (client < 0 || unlink("small-send-buffer") < 0)
        return client;

    const int size = 4096;
    if (setsockopt(client, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
        int error = errno;
        close(client);
        errno = error;
        return -1;
    }
    return client;
}

const char *__real_backend_zmq_get_status_event(size_t *len, uint64_t *revision);

/* The upload fixture requests a new status while REST and upload work is
 * active. An unchanged source only emits its initial snapshot. */
const char *__wrap_backend_zmq_get_status_event(size_t *len, uint64_t *revision)
{
    FILE *file = fopen("status-revision", "r");
    if (!file)
        return __real_backend_zmq_get_status_event(len, revision);

    uint64_t next;
    int parsed = fscanf(file, "%" SCNu64, &next);
    fclose(file);
    if (parsed != 1)
        return __real_backend_zmq_get_status_event(len, revision);

    static char event[1200];
    static uint64_t cached_revision;
    static size_t event_len;
    if (next != cached_revision) {
        int n = snprintf(event, sizeof(event), "data: {\"revision\":%" PRIu64
                         ",\"fixture\":\"%01100d\"}\n\n", next, 0);
        if (n < 0 || (size_t)n >= sizeof(event))
            return NULL;
        event_len = (size_t)n;
        cached_revision = next;
    }
    *len = event_len;
    *revision = cached_revision;
    return event;
}
