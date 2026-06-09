/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "diagnostics_log.h"
#include "print_control.h"
#include "print_state_rules.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

static FILE *diag_file;
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

static void write_timestamp(FILE *f)
{
    time_t now = time(NULL);
    struct tm tm_now;
    char buf[32];

    if (!f)
        return;

    if (localtime_r(&now, &tm_now)) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_now);
        fprintf(f, "ts=\"%s\" ", buf);
    }
}

static void write_quoted(FILE *f, const char *key, const char *value)
{
    fputs(key, f);
    fputs("=\"", f);
    for (const char *p = value ? value : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\')
            fputc('\\', f);
        if (c >= 0x20)
            fputc((int)c, f);
    }
    fputs("\" ", f);
}

static int snapshot_changed(const deneb_diag_snapshot_t *next)
{
    return !have_snapshot ||
           memcmp(&last_snapshot, next, sizeof(*next)) != 0;
}

int deneb_diagnostics_log_open(const char *path)
{
    if (diag_file)
        return 0;

    diag_file = fopen(path ? path : DENEB_PRINTSVC_DIAGNOSTIC_LOG, "a");
    if (!diag_file && (!path || strcmp(path, DENEB_PRINTSVC_DIAGNOSTIC_LOG) == 0))
        diag_file = fopen(DENEB_PRINTSVC_DIAGNOSTIC_LOG_FALLBACK, "a");
    if (!diag_file)
        return -1;

    setvbuf(diag_file, NULL, _IOLBF, 0);
    write_timestamp(diag_file);
    fputs("event=log_open component=deneb-printsvc\n", diag_file);
    return 0;
}

void deneb_diagnostics_log_close(void)
{
    if (diag_file) {
        write_timestamp(diag_file);
        fputs("event=log_close component=deneb-printsvc\n", diag_file);
        fclose(diag_file);
    }
    diag_file = NULL;
    have_snapshot = 0;
    last_status_ms = 0;
    memset(&last_snapshot, 0, sizeof(last_snapshot));
}

void deneb_diagnostics_log_status(const deneb_status_t *status, int force)
{
    deneb_diag_snapshot_t next;
    deneb_print_phase_t phase;
    long long now_ms;

    if (!diag_file || !status)
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

    now_ms = monotonic_ms();
    if (!force && !snapshot_changed(&next) && now_ms - last_status_ms < 1000)
        return;

    phase = deneb_print_control_phase_from_state(status->state);
    write_timestamp(diag_file);
    fputs("event=status component=deneb-printsvc ", diag_file);
    write_quoted(diag_file, "stock.req", status->req[0] ? status->req :
                 deneb_status_state_name(status->state));
    write_quoted(diag_file, "stock.file", status->file);
    write_quoted(diag_file, "stock.firmware", status->firmware);
    write_quoted(diag_file, "stock.machineType", status->machine_type);
    fprintf(diag_file,
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
    write_quoted(diag_file, "serial.lastFlowResponse",
                 status->flow_last_response);
    write_quoted(diag_file, "native.errorKey", status->error.key ?
                 status->error.key : DENEB_PRINT_NONE_VALUE);
    write_quoted(diag_file, "native.errorDetail", status->error.detail);
    fputc('\n', diag_file);

    last_snapshot = next;
    have_snapshot = 1;
    last_status_ms = now_ms;
}

void deneb_diagnostics_log_command(const deneb_command_t *cmd, int rc,
                                   unsigned int latency_ms)
{
    if (!diag_file || !cmd)
        return;

    write_timestamp(diag_file);
    fputs("event=command component=deneb-printsvc ", diag_file);
    write_quoted(diag_file, "command.verb", deneb_command_type_name(cmd->type));
    write_quoted(diag_file, "command.file", cmd->file);
    write_quoted(diag_file, "command.macro", cmd->macro);
    fprintf(diag_file, "command.gcodeCount=%u result=%s rc=%d latencyMs=%u\n",
            (unsigned int)cmd->gcode_count, rc == 0 ? "ok" : "error", rc,
            latency_ms);
}
