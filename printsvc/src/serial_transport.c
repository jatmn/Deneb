/* SPDX-License-Identifier: MPL-2.0 */
#include "serial_transport.h"

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static speed_t baud_to_speed(unsigned int baud)
{
    switch (baud) {
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B250000
        case 250000: return B250000;
#endif
        default: return B115200;
    }
}

static int configure_serial(int fd, unsigned int baud)
{
    struct termios tio;

    if (tcgetattr(fd, &tio) != 0)
        return -1;

    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    speed_t speed = baud_to_speed(baud);
    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0)
        return -1;

    return tcsetattr(fd, TCSANOW, &tio);
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
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }

    return 0;
}

int deneb_serial_read_line(deneb_serial_transport_t *transport,
                           char *line, size_t line_sz)
{
    size_t pos = 0;

    if (!transport || transport->fd < 0 || !line || line_sz == 0)
        return -1;

    line[0] = '\0';

    while (pos + 1 < line_sz) {
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
        if (ch == '\n')
            break;
        line[pos++] = ch;
    }

    line[pos] = '\0';
    return pos > 0 ? (int)pos : 0;
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
