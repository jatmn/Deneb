/* SPDX-License-Identifier: MPL-2.0 */
#include "gcode_control.h"

#include "command_reply.h"
#include "gcode_rewrite.h"
#include "motion_send_error.h"
#include "motion_sender.h"
#include "print_state_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int parse_heater_target(const char *line, int *is_bed, float *target)
{
    char letter;
    int code;
    const char *s;

    if (!line || !is_bed || !target)
        return 0;

    if (sscanf(line, " %c%d", &letter, &code) != 2 ||
        toupper((unsigned char)letter) != 'M')
        return 0;

    if (code == 104)
        *is_bed = 0;
    else if (code == 140)
        *is_bed = 1;
    else
        return 0;

    s = strchr(line, 'S');
    if (!s)
        s = strchr(line, 's');
    if (!s || sscanf(s + 1, "%f", target) != 1)
        return 0;

    if (*target < 0.0f)
        *target = 0.0f;
    return 1;
}

static void apply_accepted_heater_target(deneb_print_service_t *svc,
                                         const char *line)
{
    int is_bed = 0;
    float target = 0.0f;

    if (!svc || !parse_heater_target(line, &is_bed, &target))
        return;

    if (is_bed)
        svc->status.bed_t_set = target;
    else
        svc->status.head_t_set = target;
}

static int service_has_lifecycle_work(const deneb_print_service_t *svc)
{
    return svc && (svc->job_active || svc->abort_cleanup_pending ||
                   svc->finish_cleanup_pending);
}

static int heater_targets_active(const deneb_print_service_t *svc)
{
    return svc && (svc->status.bed_t_set > 0.0f ||
                   svc->status.head_t_set > 0.0f);
}

void deneb_gcode_control_refresh_manual_status(deneb_print_service_t *svc)
{
    if (!svc || service_has_lifecycle_work(svc) ||
        svc->gcode_queue_active ||
        svc->heater_wait.active ||
        svc->status.state == DENEB_PRINT_STATE_ERROR ||
        svc->status.state == DENEB_PRINT_STATE_COMPLETE)
        return;

    if (heater_targets_active(svc)) {
        svc->status.state = DENEB_PRINT_STATE_PREPARING;
        snprintf(svc->status.req, sizeof(svc->status.req), "%s",
                 DENEB_PRINT_REQ_PREHEATING);
        return;
    }

    svc->status.state = DENEB_PRINT_STATE_IDLE;
    snprintf(svc->status.req, sizeof(svc->status.req), "%s",
             DENEB_PRINT_REQ_IDLE);
}

static void apply_queued_heater_targets(deneb_print_service_t *svc)
{
    if (!svc)
        return;

    for (size_t i = 0; i < svc->gcode_queue_count; i++)
        apply_accepted_heater_target(svc, svc->gcode_queue[i]);
    deneb_gcode_control_refresh_manual_status(svc);
}

static void clear_queue(deneb_print_service_t *svc)
{
    if (!svc)
        return;
    svc->gcode_queue_count = 0;
    svc->gcode_queue_index = 0;
    svc->gcode_queue_active = 0;
}

static int enqueue_rewrite(deneb_print_service_t *svc,
                           const deneb_gcode_rewrite_t *rewrite)
{
    if (!svc || !rewrite)
        return -1;

    for (size_t i = 0; i < rewrite->count; i++) {
        size_t slot = svc->gcode_queue_count;
        if (slot >= DENEB_PRINTSVC_GCODE_QUEUE_COMMANDS)
            return -1;
        snprintf(svc->gcode_queue[slot], sizeof(svc->gcode_queue[slot]), "%s",
                 rewrite->commands[i]);
        svc->gcode_queue_wait_bed[slot] = (i == 0) && rewrite->wait_for_bed;
        svc->gcode_queue_wait_nozzle[slot] =
            (i == 0) && rewrite->wait_for_nozzle;
        svc->gcode_queue_wait_target[slot] = rewrite->wait_target;
        svc->gcode_queue_count++;
    }
    return 0;
}

int deneb_gcode_control_poll(deneb_print_service_t *svc)
{
    if (!svc || !svc->gcode_queue_active)
        return 0;

    if (svc->heater_wait.active) {
        if (!deneb_heater_wait_ready(&svc->heater_wait, &svc->status)) {
            deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
            return 0;
        }
        svc->heater_wait.active = 0;
    }

    while (svc->gcode_queue_index < svc->gcode_queue_count) {
        size_t index = svc->gcode_queue_index;
        int rc;

        if (deneb_flow_has_pending_barrier(&svc->flow) ||
            !deneb_flow_can_send(&svc->flow) ||
            deneb_flow_inflight(&svc->flow) >= DENEB_PRINTSVC_STREAM_WINDOW)
            return 0;

        rc = deneb_motion_sender_send_gcode(&svc->flow, &svc->serial,
                                            svc->serial_ready,
                                            svc->gcode_queue[index]);
        if (rc != 0) {
            if (rc == DENEB_MOTION_SEND_FLOW_FULL)
                return 0;
            clear_queue(svc);
            svc->heater_wait.active = 0;
            svc->status.error =
                deneb_error_make(deneb_motion_send_error_code(rc),
                                 "gcode failed");
            return -1;
        }
        apply_accepted_heater_target(svc, svc->gcode_queue[index]);
        svc->gcode_queue_index++;

        if (svc->gcode_queue_wait_bed[index]) {
            deneb_heater_wait_start_bed(&svc->heater_wait,
                                        svc->gcode_queue_wait_target[index],
                                        1.0f);
            deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
            return 0;
        }
        if (svc->gcode_queue_wait_nozzle[index]) {
            deneb_heater_wait_start_head(&svc->heater_wait,
                                         svc->gcode_queue_wait_target[index],
                                         1.0f);
            deneb_heater_wait_apply_status(&svc->heater_wait, &svc->status);
            return 0;
        }
    }

    clear_queue(svc);
    deneb_gcode_control_refresh_manual_status(svc);
    return 1;
}

int deneb_gcode_control_run(deneb_print_service_t *svc,
                            const deneb_command_t *cmd,
                            char *reply, size_t reply_sz)
{
    if (!svc || !cmd || !reply || reply_sz == 0)
        return -1;

    if (svc->gcode_queue_active) {
        deneb_command_reply_error(reply, reply_sz, "gcode already active");
        return -1;
    }

    clear_queue(svc);
    for (size_t i = 0; i < cmd->gcode_count; i++) {
        deneb_gcode_rewrite_t rewrite;
        if (deneb_gcode_rewrite_line(cmd->gcode[i], &rewrite) < 0) {
            deneb_command_reply_error(reply, reply_sz, "gcode failed");
            return -1;
        }
        if (enqueue_rewrite(svc, &rewrite) != 0) {
            clear_queue(svc);
            deneb_command_reply_error(reply, reply_sz, "gcode queue full");
            return -1;
        }
    }
    apply_queued_heater_targets(svc);

    if (svc->gcode_queue_count > 0) {
        int rc;
        svc->gcode_queue_active = 1;
        rc = deneb_gcode_control_poll(svc);
        if (rc < 0) {
            deneb_command_reply_error(reply, reply_sz, "gcode failed");
            return -1;
        }
    }
    deneb_gcode_control_refresh_manual_status(svc);

    deneb_command_reply_ok(reply, reply_sz, "gcode accepted");
    return 0;
}
