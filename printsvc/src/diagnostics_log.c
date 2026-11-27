/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "diagnostics_log.h"
#include "print_control.h"
#include "print_state_rules.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DENEB_DIAG_HEARTBEAT_IDLE_MS 60000LL
#define DENEB_DIAG_HEARTBEAT_ACTIVE_MS 60000LL
#define DENEB_DIAG_MAX_LOG_BYTES (1024L * 1024L)
#define DENEB_DIAG_GENERATION_BYTES (DENEB_DIAG_MAX_LOG_BYTES / 2)
#define DENEB_DIAG_RECHECK_MS 60000LL
#define DENEB_DIAG_PREVIOUS_SUFFIX ".deneb-previous"
#define DENEB_DIAG_TRIM_SUFFIX ".deneb-trim"

typedef struct {
    char text[2048];
    size_t length;
} deneb_diag_event_t;

typedef struct {
    deneb_print_state_t state;
    char req[32];
    char file[128];
    char firmware[64];
    char machine_type[16];
    int pcb_id;
    int pcb_id_valid;
    unsigned int flow_ack;
    unsigned int flow_resend;
    unsigned int flow_reject;
    unsigned int flow_inflight;
    char flow_last_response[96];
    unsigned int job_queue_depth;
    unsigned int job_line_number;
    unsigned int command_latency_ms;
    unsigned int planner_starvation_count;
    char error_detail[128];
} deneb_diag_snapshot_t;

static const char primary_path[] = DENEB_PRINTSVC_DIAGNOSTIC_LOG;
static const char fallback_path[] = DENEB_PRINTSVC_DIAGNOSTIC_LOG_FALLBACK;
static const char *diag_path;
static char *custom_path;
static int diag_fd = -1;
static off_t diag_bytes;
static long long next_check_ms;
static deneb_diag_snapshot_t last_snapshot;
static int have_snapshot;
static long long last_status_ms;

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void event_printf(deneb_diag_event_t *event, const char *format, ...)
{
    size_t space = sizeof(event->text) - event->length;
    if (space == 0)
        return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(event->text + event->length, space, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= space)
        event->length = sizeof(event->text);
    else
        event->length += (size_t)n;
}

static void event_char(deneb_diag_event_t *event, char c)
{
    if (event->length < sizeof(event->text))
        event->text[event->length++] = c;
}

static void write_timestamp(deneb_diag_event_t *event)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char buf[32];

    if (localtime_r(&now, &tm_now) &&
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_now))
        event_printf(event, "ts=\"%s\" ", buf);
}

static void write_quoted(deneb_diag_event_t *event, const char *key, const char *value)
{
    event_printf(event, "%s=\"", key);
    for (const char *p = value ? value : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\')
            event_char(event, '\\');
        if (c >= 0x20)
            event_char(event, (char)c);
    }
    event_printf(event, "\" ");
}

static int snapshot_changed(const deneb_diag_snapshot_t *next)
{
    if (!have_snapshot)
        return 1;

    return last_snapshot.state != next->state ||
           strcmp(last_snapshot.req, next->req) != 0 ||
           strcmp(last_snapshot.file, next->file) != 0 ||
           strcmp(last_snapshot.firmware, next->firmware) != 0 ||
           strcmp(last_snapshot.machine_type, next->machine_type) != 0 ||
           last_snapshot.pcb_id != next->pcb_id ||
           last_snapshot.pcb_id_valid != next->pcb_id_valid ||
           last_snapshot.flow_resend != next->flow_resend ||
           last_snapshot.flow_reject != next->flow_reject ||
           strcmp(last_snapshot.error_detail, next->error_detail) != 0;
}

static int append_bytes(int fd, const char *data, size_t length, size_t *written)
{
    while (length > 0) {
        ssize_t n = write(fd, data, length);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        if (written)
            *written += (size_t)n;
        data += n;
        length -= (size_t)n;
    }
    return 0;
}

static int log_name(char *out, size_t capacity, const char *path, const char *suffix)
{
    int n = snprintf(out, capacity, "%s%s", path, suffix);
    return n >= 0 && (size_t)n < capacity ? 0 : -1;
}

/* Shrink existing oversized files without overwriting unread diagnostic bytes. */
static int trim_log(const char *path, int fd, const struct stat *st)
{
    char tmp[PATH_MAX];
    if (log_name(tmp, sizeof(tmp), path, DENEB_DIAG_TRIM_SUFFIX) != 0 ||
        (unlink(tmp) != 0 && errno != ENOENT))
        return -1;
    int out = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0)
        return -1;

    char buf[2048];
    off_t offset = st->st_size - DENEB_DIAG_GENERATION_BYTES;
    int line_start = 0;
    int ok = fchmod(out, st->st_mode & 0777) == 0;
    while (ok && offset < st->st_size) {
        size_t count = st->st_size - offset < (off_t)sizeof(buf) ?
            (size_t)(st->st_size - offset) : sizeof(buf);
        ssize_t n = pread(fd, buf, count, offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = 0;
            break;
        }
        const char *start = buf;
        if (!line_start) {
            const char *newline = memchr(buf, '\n', (size_t)n);
            start = newline ? newline + 1 : buf + n;
            line_start = newline != NULL;
        }
        ok = append_bytes(out, start, (size_t)(buf + n - start), NULL) == 0;
        offset += n;
    }
    struct stat named;
    if (close(out) == 0 && ok && lstat(path, &named) == 0 &&
        named.st_dev == st->st_dev && named.st_ino == st->st_ino &&
        named.st_size == st->st_size && rename(tmp, path) == 0)
        return 0;
    unlink(tmp);
    return -1;
}

static int open_log_file(const char *path, int create, off_t *size)
{
    int flags = O_RDWR | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    int fd = open(path, flags | (create ? O_CREAT : 0), 0664);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    if (st.st_size > DENEB_DIAG_GENERATION_BYTES) {
        int trimmed = trim_log(path, fd, &st);
        close(fd);
        if (trimmed != 0)
            return -1;
        fd = open(path, flags);
        if (fd < 0)
            return -1;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            close(fd);
            return -1;
        }
    }
    *size = st.st_size;
    return fd;
}

static void close_log_file(void)
{
    if (diag_fd >= 0)
        close(diag_fd);
    diag_fd = -1;
}

static int open_log_location(const char *path, long long now)
{
    char previous[PATH_MAX];
    struct stat st;
    if (log_name(previous, sizeof(previous), path, DENEB_DIAG_PREVIOUS_SUFFIX) != 0)
        return -1;
    if (lstat(previous, &st) == 0 && st.st_size > DENEB_DIAG_GENERATION_BYTES) {
        off_t size;
        int fd = open_log_file(previous, 0, &size);
        if (fd < 0)
            return -1;
        close(fd);
    }
    diag_fd = open_log_file(path, 1, &diag_bytes);
    if (diag_fd < 0)
        return -1;
    diag_path = path;
    next_check_ms = now + DENEB_DIAG_RECHECK_MS;
    return 0;
}

static int refresh_log(long long now)
{
    struct stat current;
    struct stat named;
    if (fstat(diag_fd, &current) != 0)
        return -1;
    if (lstat(diag_path, &named) == 0 &&
        current.st_dev == named.st_dev && current.st_ino == named.st_ino &&
        current.st_size <= DENEB_DIAG_GENERATION_BYTES) {
        diag_bytes = current.st_size;
        next_check_ms = now + DENEB_DIAG_RECHECK_MS;
        return 0;
    }
    close_log_file();
    return open_log_location(diag_path, now);
}

static int rotate_log(long long now)
{
    char previous[PATH_MAX];
    if (log_name(previous, sizeof(previous), diag_path, DENEB_DIAG_PREVIOUS_SUFFIX) != 0 ||
        rename(diag_path, previous) != 0)
        return -1;
    close_log_file();
    return open_log_location(diag_path, now);
}

static int write_event(const deneb_diag_event_t *event, long long now)
{
    if ((now >= next_check_ms ||
         diag_bytes + (off_t)event->length > DENEB_DIAG_GENERATION_BYTES) &&
        refresh_log(now) != 0)
        return -1;
    if (diag_bytes + (off_t)event->length > DENEB_DIAG_GENERATION_BYTES &&
        rotate_log(now) != 0)
        return -1;
    if (diag_bytes + (off_t)event->length > DENEB_DIAG_GENERATION_BYTES)
        return -1;
    size_t written = 0;
    if (append_bytes(diag_fd, event->text, event->length, &written) != 0) {
        struct stat st;
        /* Remove a partial line only if its original boundary still agrees. */
        if (written && fstat(diag_fd, &st) == 0 &&
            st.st_size == diag_bytes + (off_t)written)
            (void)ftruncate(diag_fd, diag_bytes);
        return -1;
    }
    diag_bytes += (off_t)event->length;
    return 0;
}

static int open_selected_log(const char *path, long long now)
{
    if (open_log_location(path, now) == 0)
        return 0;
    return path == primary_path ? open_log_location(fallback_path, now) : -1;
}

static int log_ready(long long now)
{
    if (diag_fd >= 0)
        return 1;
    if (!diag_path || now < next_check_ms)
        return 0;
    if (open_selected_log(diag_path, now) == 0)
        return 1;
    next_check_ms = now + DENEB_DIAG_RECHECK_MS;
    return 0;
}

static void publish_event(const deneb_diag_event_t *event, long long now)
{
    if (event->length >= sizeof(event->text) || write_event(event, now) == 0)
        return;
    int try_fallback = diag_path == primary_path;
    close_log_file();
    if (try_fallback && open_log_location(fallback_path, now) == 0 &&
        write_event(event, now) == 0)
        return;
    close_log_file();
    next_check_ms = now + DENEB_DIAG_RECHECK_MS;
}

static void lifecycle_event(const char *name, long long now)
{
    deneb_diag_event_t event;
    event.length = 0;
    write_timestamp(&event);
    event_printf(&event, "event=%s component=deneb-printsvc\n", name);
    publish_event(&event, now);
}

int deneb_diagnostics_log_open(const char *path)
{
    if (diag_fd >= 0)
        return 0;
    int is_default = !path || strcmp(path, primary_path) == 0;
    diag_path = NULL;
    free(custom_path);
    custom_path = is_default ? NULL : strdup(path);
    if (!is_default && !custom_path)
        return -1;

    long long now = monotonic_ms();
    if (open_selected_log(is_default ? primary_path : custom_path, now) != 0)
        return -1;

    lifecycle_event("log_open", now);
    return diag_fd >= 0 ? 0 : -1;
}

void deneb_diagnostics_log_close(void)
{
    if (diag_fd >= 0)
        lifecycle_event("log_close", monotonic_ms());
    close_log_file();
    free(custom_path);
    custom_path = NULL;
    diag_path = NULL;
    have_snapshot = 0;
    last_status_ms = 0;
    memset(&last_snapshot, 0, sizeof(last_snapshot));
}

void deneb_diagnostics_log_status(const deneb_status_t *status, int force)
{
    deneb_diag_snapshot_t next;
    deneb_print_phase_t phase;
    long long now_ms;
    long long heartbeat_ms;
    int changed;

    if (!status || !diag_path)
        return;
    now_ms = monotonic_ms();
    if (!log_ready(now_ms))
        return;

    memset(&next, 0, sizeof(next));
    next.state = status->state;
    snprintf(next.req, sizeof(next.req), "%s", status->req);
    snprintf(next.file, sizeof(next.file), "%s", status->file);
    snprintf(next.firmware, sizeof(next.firmware), "%s", status->firmware);
    snprintf(next.machine_type, sizeof(next.machine_type), "%s",
             status->machine_type);
    next.pcb_id = status->pcb_id;
    next.pcb_id_valid = status->pcb_id_valid ? 1 : 0;
    next.flow_ack = status->flow_ack;
    next.flow_resend = status->flow_resend;
    next.flow_reject = status->flow_reject;
    next.flow_inflight = status->flow_inflight;
    snprintf(next.flow_last_response, sizeof(next.flow_last_response), "%s",
             status->flow_last_response);
    next.job_queue_depth = status->job_queue_depth;
    next.job_line_number = status->job_line_number;
    next.command_latency_ms = status->command_latency_ms;
    next.planner_starvation_count = status->planner_starvation_count;
    snprintf(next.error_detail, sizeof(next.error_detail), "%s",
             status->error.detail);

    phase = deneb_print_control_phase_from_state(status->state);
    heartbeat_ms = phase == DENEB_PRINT_PHASE_IDLE ?
        DENEB_DIAG_HEARTBEAT_IDLE_MS : DENEB_DIAG_HEARTBEAT_ACTIVE_MS;
    changed = snapshot_changed(&next);
    if (!force && !changed &&
        last_status_ms > 0 && now_ms - last_status_ms < heartbeat_ms)
        return;

    deneb_diag_event_t event;
    event.length = 0;
    write_timestamp(&event);
    event_printf(&event, "event=status component=deneb-printsvc ");
    write_quoted(&event, "stock.req", status->req[0] ? status->req :
                 deneb_status_state_name(status->state));
    write_quoted(&event, "stock.file", status->file);
    write_quoted(&event, "stock.firmware", status->firmware);
    write_quoted(&event, "stock.machineType", status->machine_type);
    event_printf(&event,
            "stock.pcbId=%d stock.pcbIdValid=%d "
            "stock.headTcur=%.1f stock.headTset=%.1f "
            "stock.bedTcur=%.1f stock.bedTset=%.1f "
            "stock.x=%.3f stock.y=%.3f stock.z=%.3f stock.fault=%d "
            "native.phase=%s native.active=%d native.stopAllowed=%d "
            "serial.flowInflight=%u serial.ack=%u serial.resend=%u serial.reject=%u "
            "native.queueDepth=%u native.jobLine=%u native.commandLatencyMs=%u "
            "native.plannerStarvation=%u native.positionReports=%u "
            "native.finishDrainTicks=%u native.finishStableReports=%u ",
            status->pcb_id, status->pcb_id_valid ? 1 : 0,
            status->head_t_cur, status->head_t_set,
            status->bed_t_cur, status->bed_t_set,
            status->x, status->y, status->z, status->fault ? 1 : 0,
            deneb_print_control_phase_name(phase),
            deneb_status_has_active_print(status) ? 1 : 0,
            deneb_print_control_phase_stop_allowed(phase) ? 1 : 0,
            status->flow_inflight, status->flow_ack,
            status->flow_resend, status->flow_reject,
            status->job_queue_depth, status->job_line_number,
            status->command_latency_ms, status->planner_starvation_count,
            status->position_report_count, status->finish_drain_ticks,
            status->finish_stable_reports);
    write_quoted(&event, "serial.lastFlowResponse",
                 status->flow_last_response);
    write_quoted(&event, "native.errorKey", status->error.key ?
                 status->error.key : DENEB_PRINT_NONE_VALUE);
    write_quoted(&event, "native.errorDetail", status->error.detail);
    event_char(&event, '\n');
    publish_event(&event, now_ms);

    last_snapshot = next;
    have_snapshot = 1;
    last_status_ms = now_ms;
}

void deneb_diagnostics_log_command(const deneb_command_t *cmd, int rc,
                                   unsigned int latency_ms)
{
    if (!cmd || !diag_path)
        return;
    long long now = monotonic_ms();
    if (!log_ready(now))
        return;

    deneb_diag_event_t event;
    event.length = 0;
    write_timestamp(&event);
    event_printf(&event, "event=command component=deneb-printsvc ");
    write_quoted(&event, "command.verb", deneb_command_type_name(cmd->type));
    write_quoted(&event, "command.file", cmd->file);
    write_quoted(&event, "command.macro", cmd->macro);
    event_printf(&event, "command.gcodeCount=%u result=%s rc=%d latencyMs=%u\n",
            (unsigned int)cmd->gcode_count, rc == 0 ? "ok" : "error", rc,
            latency_ms);
    publish_event(&event, now);
}
