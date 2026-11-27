/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

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
