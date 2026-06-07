/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTSVC_SERIAL_TRANSPORT_H
#define DENEB_PRINTSVC_SERIAL_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    char device[128];
    unsigned int baud;
} deneb_serial_transport_t;

int deneb_serial_open(deneb_serial_transport_t *transport, const char *device,
                      unsigned int baud);
int deneb_serial_write_all(deneb_serial_transport_t *transport,
                           const uint8_t *data, size_t len);
int deneb_serial_read_line(deneb_serial_transport_t *transport,
                           char *line, size_t line_sz);
void deneb_serial_close(deneb_serial_transport_t *transport);

#endif
