/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "print_history.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int __real_rename(const char *old_path, const char *new_path);

int __wrap_rename(const char *old_path, const char *new_path)
{
    const char *mode = getenv("DENEB_HISTORY_TEST_RENAME");
    if (mode && strcmp(mode, "fail") == 0) {
        errno = EIO;
        return -1;
    }
    if (mode && strcmp(mode, "hold") == 0) {
        puts("ready");
        fflush(stdout);
        for (;;)
            pause();
    }
    return __real_rename(old_path, new_path);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "limit") == 0) {
        printf("%d\n", DENEB_PRINT_HISTORY_MAX_BYTES);
        return 0;
    }
    if (argc != 4)
        return 2;
    int first = atoi(argv[1]);
    int count = atoi(argv[2]);
    const struct timespec delay = { .tv_nsec = atol(argv[3]) * 1000000L };

    for (int i = first; i < first + count; i++) {
        char name[32];
        char uuid[32];
        snprintf(name, sizeof(name), "part-%05d.gcode", i);
        snprintf(uuid, sizeof(uuid), "job-%05d", i);
        const deneb_print_history_entry_t entry = {
            .name = name, .uuid = uuid, .source = "USB", .state = "completed",
            .time_total = 120, .time_elapsed = 120, .progress = 100.0f,
            .started_at = 1700000000LL + i, .finished_at = 1700000120LL + i
        };
        if (deneb_print_history_append_entry(DENEB_PRINT_HISTORY_PATH, &entry) != 0)
            return 1;
        if (delay.tv_nsec > 0)
            nanosleep(&delay, NULL);
    }
    return 0;
}
