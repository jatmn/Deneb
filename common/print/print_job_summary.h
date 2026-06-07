/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_JOB_SUMMARY_H
#define DENEB_PRINT_JOB_SUMMARY_H

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

#endif
