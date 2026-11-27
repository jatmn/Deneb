/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_FILE_WRITE_H
#define DENEB_FILE_WRITE_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static inline FILE *deneb_fopen_write(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return NULL;

    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
    }
    return file;
}

#endif
