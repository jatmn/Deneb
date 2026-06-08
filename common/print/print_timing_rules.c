/* SPDX-License-Identifier: MPL-2.0 */
#include "print_state_rules.h"

int deneb_print_elapsed_seconds(int time_total, int time_left)
{
    if (time_total <= 0)
        return 0;
    if (time_left <= 0)
        return time_total;
    if (time_left >= time_total)
        return 0;
    return time_total - time_left;
}

float deneb_print_progress_percent(int time_total, int time_left)
{
    int elapsed = deneb_print_elapsed_seconds(time_total, time_left);

    if (time_total <= 0 || elapsed <= 0)
        return 0.0f;
    if (elapsed >= time_total)
        return 100.0f;
    return (float)elapsed * 100.0f / (float)time_total;
}

float deneb_print_progress_fraction(float progress_percent)
{
    if (progress_percent <= 0.0f)
        return 0.0f;
    if (progress_percent >= 100.0f)
        return 1.0f;
    return progress_percent / 100.0f;
}

void deneb_print_normalize_timing(int is_printing,
                                  int is_paused,
                                  int *time_total,
                                  int *time_left,
                                  float *progress_percent)
{
    int total = time_total ? *time_total : 0;
    int left = time_left ? *time_left : 0;

    if (total > 0 && left >= 0 && !is_printing && !is_paused) {
        total = 0;
        left = 0;
    }

    if (total > 0 && left > total)
        left = total;

    if (time_total)
        *time_total = total;
    if (time_left)
        *time_left = left;
    if (progress_percent)
        *progress_percent = deneb_print_progress_percent(total, left);
}
