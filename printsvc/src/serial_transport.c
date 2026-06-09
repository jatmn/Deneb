/* SPDX-License-Identifier: MPL-2.0 */
#include "serial_transport.h"

#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int wait_for_serial_write(int fd)
{
    struct pollfd pfd;
    int rc;

    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    do {
        rc = poll(&pfd, 1, DENEB_PRINTSVC_SERIAL_WRITE_TIMEOUT_MS);
    } while (rc < 0 && errno == EINTR);

    if (rc <= 0)
        return -1;
    return (pfd.revents & POLLOUT) ? 0 : -1;
}

static int configure_serial(int fd, unsigned int baud)
{
    struct termios2 tio;

    if (ioctl(fd, TCGETS2, &tio) != 0)
        return -1;

    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                     INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS | CBAUD);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag |= CS8 | BOTHER;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;

    return ioctl(fd, TCSETS2, &tio);
}

int deneb_serial_open(deneb_serial_transport_t *transport, const char *device,
                      unsigned int baud)
{
    if (!transport || !device)
        return -1;

    memset(transport, 0, sizeof(*transport));
    transport->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (transport->fd < 0)
        return -1;

    if (configure_serial(transport->fd, baud) != 0) {
        close(transport->fd);
        transport->fd = -1;
        return -1;
    }

    strncpy(transport->device, device, sizeof(transport->device) - 1);
    transport->baud = baud;
    return 0;
}

int deneb_serial_write_all(deneb_serial_transport_t *transport,
                           const uint8_t *data, size_t len)
{
    size_t off = 0;

    if (!transport || transport->fd < 0 || !data)
        return -1;

    while (off < len) {
        ssize_t n = write(transport->fd, data + off, len - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_for_serial_write(transport->fd) == 0)
                continue;
            return -1;
        }
        if (n == 0) {
            if (wait_for_serial_write(transport->fd) == 0)
                continue;
            return -1;
        }
        return -1;
    }

    return 0;
}

int deneb_serial_read_line(deneb_serial_transport_t *transport,
                           char *line, size_t line_sz)
{
    if (!transport || transport->fd < 0 || !line || line_sz == 0)
        return -1;

    line[0] = '\0';

    for (;;) {
        char ch;
        ssize_t n = read(transport->fd, &ch, 1);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return -1;
        }

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            size_t copy_len = transport->rx_len;
            if (copy_len >= line_sz)
                copy_len = line_sz - 1;
            memcpy(line, transport->rx_buf, copy_len);
            line[copy_len] = '\0';
            transport->rx_len = 0;
            return (int)copy_len;
        }
        if (transport->rx_len + 1 < sizeof(transport->rx_buf)) {
            transport->rx_buf[transport->rx_len++] = ch;
        } else {
            transport->rx_len = 0;
            return -1;
        }
    }

    return 0;
}

void deneb_serial_close(deneb_serial_transport_t *transport)
{
    if (!transport)
        return;
    if (transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}
