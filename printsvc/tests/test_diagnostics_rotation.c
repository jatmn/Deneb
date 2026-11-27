/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#undef NDEBUG
#include "config.h"
#include "diagnostics_log.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PRIMARY DENEB_PRINTSVC_DIAGNOSTIC_LOG
#define FALLBACK DENEB_PRINTSVC_DIAGNOSTIC_LOG_FALLBACK
#define PREVIOUS ".deneb-previous"
#define LIMIT (1024 * 1024)

static time_t fixture_seconds = 1;

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *time)
{
    assert(clock_id == CLOCK_MONOTONIC);
    time->tv_sec = fixture_seconds;
    time->tv_nsec = 0;
    return 0;
}

static void emit_fault(unsigned int line, const char *detail)
{
    deneb_status_t status = { 0 };
    status.job_line_number = line;
    status.fault = 1;
    snprintf(status.error.detail, sizeof(status.error.detail), "%s", detail);
    deneb_diagnostics_log_status(&status, 1);
}

static struct stat log_stat(const char *path)
{
    struct stat st = { 0 };
    if (stat(path, &st) != 0)
        assert(errno == ENOENT);
    return st;
}

static int log_contains(const char *path, const char *fragment)
{
    char line[2048];
    FILE *file = fopen(path, "r");
    assert(file);
    int found = 0;
    while (fgets(line, sizeof(line), file)) {
        assert(line[strlen(line) - 1] == '\n');
        found |= strstr(line, fragment) != NULL;
    }
    assert(!ferror(file));
    assert(fclose(file) == 0);
    return found;
}

static void remove_log(const char *path)
{
    if (unlink(path) != 0)
        assert(errno == ENOENT);
}

static void test_continuous_rotation(const char *path, const char *archive)
{
    assert(deneb_diagnostics_log_open(NULL) == 0);
    emit_fault(0, "original fault");
    ino_t previous_inode = 0;
    unsigned int rotations = 0;
    off_t high_water = 0;

    for (unsigned int i = 1; i <= 4000; i++) {
        emit_fault(i, "continuous fault");
        struct stat active = log_stat(path);
        struct stat previous = log_stat(archive);
        assert(active.st_size > 0 && active.st_size <= LIMIT / 2);
        assert(previous.st_size <= LIMIT / 2);
        off_t total = active.st_size + previous.st_size;
        assert(total <= LIMIT);
        if (total > high_water)
            high_water = total;
        if (previous.st_ino != previous_inode) {
            char marker[64];
            snprintf(marker, sizeof(marker), "native.jobLine=%u ", i - 1);
            assert(log_contains(archive, marker));
            previous_inode = previous.st_ino;
            rotations++;
        }
    }
    assert(rotations >= 3);
    assert(log_contains(path, "native.jobLine=4000 "));
    assert(!log_contains(archive, "original fault"));
    deneb_diagnostics_log_close();
    assert(deneb_diagnostics_log_open(NULL) == 0);
    emit_fault(4001, "fault after restart");
    assert(log_contains(path, "native.jobLine=4000 "));
    assert(log_contains(path, "fault after restart"));
    assert(log_stat(archive).st_ino == previous_inode);
    deneb_diagnostics_log_close();
    printf("%s: rotations=%u peak_managed_bytes=%lld\n",
           path, rotations, (long long)high_water);
    remove_log(path);
    remove_log(archive);
}

static void test_legacy_and_external_rotation(void)
{
    FILE *legacy = fopen(PRIMARY, "w");
    assert(legacy);
    for (unsigned int i = 0; i < 20000; i++)
        assert(fputs("legacy diagnostic before the retained tail\n", legacy) >= 0);
    assert(fputs("last useful legacy fault\n", legacy) >= 0);
    assert(fclose(legacy) == 0);
    assert(log_stat(PRIMARY).st_size > LIMIT / 2);

    assert(deneb_diagnostics_log_open(NULL) == 0);
    assert(log_stat(PRIMARY).st_size <= LIMIT / 2);
    assert(log_contains(PRIMARY, "last useful legacy fault") ||
           log_contains(PRIMARY PREVIOUS, "last useful legacy fault"));
    assert(rename(PRIMARY, PRIMARY ".external") == 0);
    off_t external_bytes = log_stat(PRIMARY ".external").st_size;
    fixture_seconds += 61;
    emit_fault(1, "fault after external rename");
    assert(log_contains(PRIMARY, "fault after external rename"));
    assert(log_stat(PRIMARY ".external").st_size == external_bytes);

    assert(truncate(PRIMARY, 0) == 0);
    fixture_seconds += 61;
    emit_fault(2, "fault after external truncate");
    assert(log_contains(PRIMARY, "fault after external truncate"));
    assert(!log_contains(PRIMARY, "fault after external rename"));
    deneb_diagnostics_log_close();
    remove_log(PRIMARY);
    remove_log(PRIMARY PREVIOUS);
    remove_log(PRIMARY ".external");
}

static void test_oversize_in_place(void)
{
    char line[1000];
    memset(line, 'x', sizeof(line));
    line[sizeof(line) - 1] = '\n';
    FILE *legacy = fopen(PRIMARY, "w");
    assert(legacy);
    for (unsigned int i = 0; i < 1200; i++)
        assert(fwrite(line, 1, sizeof(line), legacy) == sizeof(line));
    memcpy(line, "most recent complete fault", sizeof("most recent complete fault") - 1);
    assert(fwrite(line, 1, sizeof(line), legacy) == sizeof(line));
    assert(fputs("interrupted legacy record", legacy) >= 0);
    assert(fclose(legacy) == 0);
    struct stat before = log_stat(PRIMARY);
    assert(before.st_size > LIMIT);
    /* An existing writable file can be compacted without a directory entry. */
    assert(chmod("primary", 0500) == 0);
    assert(deneb_diagnostics_log_open(NULL) == 0);
    assert(log_stat(PRIMARY).st_ino == before.st_ino);
    assert(log_stat(PRIMARY).st_size <= LIMIT / 2);
    assert(log_contains(PRIMARY, "most recent complete fault"));
    assert(!log_contains(PRIMARY, "interrupted legacy record"));
    deneb_diagnostics_log_close();
    assert(chmod("primary", 0700) == 0);
    remove_log(PRIMARY);
}

static void test_write_failure(void)
{
    struct rlimit saved;
    assert(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    assert(signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
    assert(deneb_diagnostics_log_open(NULL) == 0);
    for (unsigned int i = 1; i <= 8; i++)
        emit_fault(i, "primary fault retained under write failure");

    off_t original_size = log_stat(PRIMARY).st_size;
    struct rlimit limit = { .rlim_cur = (rlim_t)original_size + 8,
                            .rlim_max = saved.rlim_max };
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    emit_fault(9, "fault sent to fallback after a partial write");
    assert(log_stat(PRIMARY).st_size == original_size);
    assert(log_contains(PRIMARY, "native.jobLine=8 "));
    assert(log_contains(FALLBACK, "fault sent to fallback after a partial write"));

    limit.rlim_cur = (rlim_t)log_stat(FALLBACK).st_size;
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    emit_fault(10, "fault while fallback cannot write");
    assert(setrlimit(RLIMIT_FSIZE, &saved) == 0);
    fixture_seconds += 61;
    emit_fault(11, "fault after storage recovers");
    assert(log_contains(PRIMARY, "fault after storage recovers"));
    assert(!log_contains(FALLBACK, "fault after storage recovers"));
    assert(!log_contains(FALLBACK, "fault while fallback cannot write"));
    deneb_diagnostics_log_close();

    assert(deneb_diagnostics_log_open(NULL) == 0);
    emit_fault(12, "fault after reopening primary");
    assert(log_contains(PRIMARY, "fault after reopening primary"));
    deneb_diagnostics_log_close();
    remove_log(PRIMARY);
    remove_log(FALLBACK);
}

static void test_copytruncate_and_short_write(void)
{
    struct rlimit saved;
    assert(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    assert(deneb_diagnostics_log_open(NULL) == 0);
    for (unsigned int i = 1; i <= 4; i++)
        emit_fault(i, "fault before external truncation");

    FILE *replaced = fopen(PRIMARY, "w");
    assert(replaced);
    assert(fputs("last complete line after truncation\n", replaced) >= 0);
    assert(fclose(replaced) == 0);
    off_t boundary = log_stat(PRIMARY).st_size;
    struct rlimit limit = { .rlim_cur = (rlim_t)boundary + 8,
                            .rlim_max = saved.rlim_max };
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    /* Leave the monotonic clock unchanged so the byte counter is stale. */
    emit_fault(5, "fault during short write");
    assert(setrlimit(RLIMIT_FSIZE, &saved) == 0);
    assert(log_stat(PRIMARY).st_size == boundary);
    assert(log_contains(PRIMARY, "last complete line after truncation"));

    /* A stopped process may have left part of an event on an earlier run. */
    FILE *incomplete = fopen(PRIMARY, "a");
    assert(incomplete);
    assert(fputs("interrupted diagnostic", incomplete) >= 0);
    assert(fclose(incomplete) == 0);
    assert(deneb_diagnostics_log_open(NULL) == 0);
    emit_fault(6, "fault after short write and reopen");
    assert(log_contains(PRIMARY, "last complete line after truncation"));
    assert(log_contains(PRIMARY, "fault after short write and reopen"));
    assert(!log_contains(PRIMARY, "interrupted diagnostic"));
    deneb_diagnostics_log_close();
    remove_log(PRIMARY);
    remove_log(FALLBACK);
}

static void test_custom_write_failure(void)
{
    const char *path = "primary/custom.log";
    struct rlimit saved;
    assert(getrlimit(RLIMIT_FSIZE, &saved) == 0);
    assert(deneb_diagnostics_log_open(path) == 0);
    struct rlimit limit = { .rlim_cur = 0, .rlim_max = saved.rlim_max };
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    emit_fault(1, "failed custom fault");
    assert(setrlimit(RLIMIT_FSIZE, &saved) == 0);
    fixture_seconds += 61;
    emit_fault(2, "custom fault after recovery");
    assert(log_contains(path, "custom fault after recovery"));
    assert(!log_stat(PRIMARY).st_ino);
    assert(!log_stat(FALLBACK).st_ino);
    deneb_diagnostics_log_close();
    remove_log(path);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char dir[PATH_MAX];
    int n = snprintf(dir, sizeof(dir), "%s/deneb-log-test.XXXXXX", tmp ? tmp : "/tmp");
    assert(n > 0 && (size_t)n < sizeof(dir));
    assert(mkdtemp(dir));
    assert(chdir(dir) == 0);
    assert(mkdir("fallback", 0700) == 0);
    test_continuous_rotation(FALLBACK, FALLBACK PREVIOUS);
    assert(mkdir("primary", 0700) == 0);
    test_continuous_rotation(PRIMARY, PRIMARY PREVIOUS);
    test_legacy_and_external_rotation();
    test_oversize_in_place();
    test_write_failure();
    test_copytruncate_and_short_write();
    test_custom_write_failure();
    assert(rmdir("primary") == 0);
    assert(rmdir("fallback") == 0);
    assert(chdir("/") == 0);
    assert(rmdir(dir) == 0);
    return 0;
}
