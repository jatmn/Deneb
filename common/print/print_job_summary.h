/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_JOB_SUMMARY_H
#define DENEB_PRINT_JOB_SUMMARY_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *uuid;
    const char *source;
    const char *state;
    int active;
    int started;
    int time_total;
    int time_left;
    int time_elapsed;
    float progress_percent;
    float progress_fraction;
} deneb_print_job_summary_t;

typedef enum {
    DENEB_PRINT_JOB_SUMMARY_FIELD_NAME,
    DENEB_PRINT_JOB_SUMMARY_FIELD_UUID,
    DENEB_PRINT_JOB_SUMMARY_FIELD_SOURCE,
    DENEB_PRINT_JOB_SUMMARY_FIELD_STATE,
    DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_STARTED,
    DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_FINISHED
} deneb_print_job_summary_string_field_t;

void deneb_print_job_summary_init(deneb_print_job_summary_t *summary,
                                  const char *name,
                                  const char *uuid,
                                  const char *source,
                                  int has_error,
                                  int is_paused,
                                  int is_printing,
                                  int time_total,
                                  int time_left,
                                  float progress_percent);
void deneb_print_job_summary_init_queued(deneb_print_job_summary_t *summary,
                                         const char *name);
int deneb_print_job_summary_format_queued_response(const char *message,
                                                   const char *name,
                                                   char *out,
                                                   size_t out_sz);
int deneb_print_job_summary_format_um_response(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_deneb_current_response(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_string_field(
    const deneb_print_job_summary_t *summary,
    deneb_print_job_summary_string_field_t field,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_progress_fraction(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_time_elapsed(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_time_total(
    const deneb_print_job_summary_t *summary,
    char *out,
    size_t out_sz);
int deneb_print_job_summary_format_cluster_active_response(
    const deneb_print_job_summary_t *summary,
    const char *printer_uuid,
    const char *created_at,
    char *out,
    size_t out_sz);

#endif
