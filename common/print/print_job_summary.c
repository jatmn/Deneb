/* SPDX-License-Identifier: MPL-2.0 */
#include "print_job_summary.h"

#include "json_string.h"
#include "print_state_rules.h"

#include <stdio.h>

void deneb_print_job_summary_init(deneb_print_job_summary_t *summary,
                                  const char *name,
                                  const char *uuid,
                                  const char *source,
                                  int has_error,
                                  int is_paused,
                                  int is_printing,
                                  int time_total,
                                  int time_left,
                                  float progress_percent)
{
    if (!summary)
        return;

    summary->name = deneb_print_job_name_or_default(name);
    summary->uuid = deneb_print_job_uuid_or_default(uuid);
    summary->source = deneb_print_job_source_or_default(source);
    summary->state = deneb_print_job_state_or_none(has_error, is_paused,
                                                   is_printing);
    summary->active = deneb_print_job_is_active(has_error, is_paused,
                                                is_printing);
    summary->started = is_printing || is_paused;
    summary->time_total = time_total;
    summary->time_left = time_left;
    summary->time_elapsed = deneb_print_elapsed_seconds(time_total, time_left);
    summary->progress_percent = progress_percent;
    summary->progress_fraction =
        deneb_print_progress_fraction(progress_percent);
}

void deneb_print_job_summary_init_queued(deneb_print_job_summary_t *summary,
                                         const char *name)
{
    if (!summary)
        return;

    deneb_print_job_summary_init(summary,
                                 name && name[0] ? name : "Print job",
                                 DENEB_PRINT_STOCK_API_JOB_UUID,
                                 DENEB_PRINT_WEB_API_JOB_SOURCE,
                                 0, 0, 1, 0, 0, 0.0f);
    summary->state = DENEB_PRINT_PHASE_NAME_PRE_PRINT;
    summary->active = 1;
    summary->started = 0;
    summary->time_elapsed = 0;
    summary->progress_fraction = 0.0f;
    summary->progress_percent = 0.0f;
}

int deneb_print_job_summary_format_queued_response(const char *message,
                                                   const char *name,
                                                   char *out,
                                                   size_t out_sz)
{
    deneb_print_job_summary_t summary;
    char msg[192];
    char safe_name[256];
    char uuid[128];
    char source[80];
    char state[64];
    int n;

    if (!out || out_sz == 0)
        return -1;

    deneb_print_job_summary_init_queued(&summary, name);
    deneb_json_escape_string(message ? message : "", msg, sizeof(msg));
    deneb_json_escape_string(summary.name, safe_name, sizeof(safe_name));
    deneb_json_escape_string(summary.uuid, uuid, sizeof(uuid));
    deneb_json_escape_string(summary.source, source, sizeof(source));
    deneb_json_escape_string(summary.state, state, sizeof(state));

    n = snprintf(out, (size_t)out_sz,
                 "{\"message\":\"%s\",\"name\":\"%s\",\"uuid\":\"%s\","
                 "\"source\":\"%s\",\"state\":\"%s\",\"progress\":%.1f,"
                 "\"time_elapsed\":%d,\"time_total\":%d}",
                 msg, safe_name, uuid, source, state,
                 summary.progress_fraction, summary.time_elapsed,
                 summary.time_total);
    if (n < 0 || (size_t)n >= out_sz)
        return -1;
    return n;
}
