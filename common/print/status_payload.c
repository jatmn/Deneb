/* SPDX-License-Identifier: MPL-2.0 */
#include "status_payload.h"

#include "json_field.h"
#include "pending_job_file.h"

#include <stdio.h>
#include <string.h>

void deneb_status_payload_init(deneb_status_payload_t *payload)
{
    if (!payload)
        return;
    memset(payload, 0, sizeof(*payload));
}

void deneb_status_filename_context_init(
    deneb_status_filename_context_t *ctx,
    const char *req,
    const char *filename,
    const char *uuid,
    int time_total,
    int time_left,
    float bed_target,
    float nozzle_target,
    int is_printing,
    int is_paused)
{
    if (!ctx)
        return;

    memset(ctx, 0, sizeof(*ctx));
    ctx->req = req;
    ctx->filename = filename;
    ctx->uuid = uuid;
    ctx->time_total = time_total;
    ctx->time_left = time_left;
    ctx->bed_target = bed_target;
    ctx->nozzle_target = nozzle_target;
    ctx->is_printing = is_printing;
    ctx->is_paused = is_paused;
}

deneb_status_filename_context_t deneb_status_filename_context_from_fields(
    const char *req,
    const char *filename,
    const char *uuid,
    int time_total,
    int time_left,
    float bed_target,
    float nozzle_target,
    int is_printing,
    int is_paused)
{
    deneb_status_filename_context_t ctx;

    deneb_status_filename_context_init(&ctx, req, filename, uuid, time_total,
                                       time_left, bed_target, nozzle_target,
                                       is_printing, is_paused);
    return ctx;
}

static void copy_json_value(const char *json, const char *key,
                            char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    if (deneb_json_get_value(json, key, out, out_sz) != 0)
        out[0] = '\0';
}

int deneb_status_payload_parse(const char *json,
                               deneb_status_payload_t *payload)
{
    char name[256];
    int topcap_present = 0;
    int native_active = 0;
    int native_stop_allowed = 0;

    if (!json || !payload)
        return -1;

    deneb_status_payload_init(payload);
    payload->nozzle_temp_cur = deneb_json_get_float(json, "headTcur", 0.0f);
    payload->nozzle_temp_set = deneb_json_get_float(json, "headTset", 0.0f);
    payload->bed_temp_cur = deneb_json_get_float(json, "bedTcur", 0.0f);
    payload->bed_temp_set = deneb_json_get_float(json, "bedTset", 0.0f);
    payload->topcap_temp_cur = deneb_json_get_float(json, "topcapTemperature", 0.0f);
    if (deneb_json_get_truthy_value(json, "topcapIsPresent",
                                    &topcap_present) == 0)
        payload->topcap_present = topcap_present;

    payload->pos_x = deneb_json_get_float(json, "X", 0.0f);
    payload->pos_y = deneb_json_get_float(json, "Y", 0.0f);
    payload->pos_z = deneb_json_get_float(json, "Z", 0.0f);
    payload->pos_e = deneb_json_get_float(json, "E", 0.0f);
    payload->time_total = deneb_json_get_int(json, "Ttot", 0);
    payload->time_left = deneb_json_get_int(json, "Tleft", 0);
    payload->progress = deneb_print_progress_percent(payload->time_total,
                                                     payload->time_left);

    copy_json_value(json, "file", payload->file, sizeof(payload->file));
    copy_json_value(json, "name", name, sizeof(name));
    if ((!payload->file[0] || strcmp(payload->file, DENEB_PRINT_NONE_VALUE) == 0) &&
        name[0] && strcmp(name, DENEB_PRINT_NONE_VALUE) != 0) {
        snprintf(payload->file, sizeof(payload->file), "%s", name);
    }
    payload->has_file = payload->file[0] &&
        strcmp(payload->file, DENEB_PRINT_NONE_VALUE) != 0;

    copy_json_value(json, "source", payload->source, sizeof(payload->source));
    copy_json_value(json, "uuid", payload->uuid, sizeof(payload->uuid));
    copy_json_value(json, "req", payload->req, sizeof(payload->req));

    deneb_print_observation_init(&payload->observation, payload->req,
                                 payload->file, payload->time_total,
                                 payload->time_left, payload->bed_temp_set,
                                 payload->nozzle_temp_set);

    payload->is_paused = deneb_print_req_is_paused(payload->req);
    payload->is_printing = deneb_print_req_is_abort(payload->req) ?
        0 : deneb_print_observation_has_context(&payload->observation);
    if (deneb_json_get_truthy_value(json, "denebActive",
                                    &native_active) == 0) {
        payload->has_native_active = 1;
        payload->native_active = native_active;
        if (!deneb_print_req_is_abort(payload->req))
            payload->is_printing = native_active;
    }
    if (deneb_json_get_truthy_value(json, "denebStopAllowed",
                                    &native_stop_allowed) == 0) {
        payload->has_native_stop_allowed = 1;
        payload->native_stop_allowed = native_stop_allowed;
    }
    payload->has_error = deneb_json_get_int(json, "received_faults", 0) != 0;
    return 0;
}

static int context_has_temp_targets(const deneb_status_filename_context_t *ctx)
{
    return ctx && deneb_print_has_temp_targets(ctx->bed_target,
                                               ctx->nozzle_target);
}

int deneb_status_payload_should_hold_filename(
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev)
{
    if (!curr || !prev || deneb_print_req_is_abort(curr->req))
        return 0;

    return curr->is_printing || curr->is_paused ||
           prev->is_printing || prev->is_paused ||
           (curr->time_total > 0 && curr->time_left >= 0) ||
           (prev->time_total > 0 && prev->time_left >= 0) ||
           context_has_temp_targets(curr) ||
           deneb_print_req_is_lifecycle(curr->req) ||
           deneb_print_req_is_lifecycle(prev->req) ||
           (curr->uuid && prev->uuid && curr->uuid[0] && prev->uuid[0] &&
            strcmp(curr->uuid, prev->uuid) == 0);
}

static void set_display_or_empty(char *dst, size_t dst_sz, const char *value)
{
    if (!dst || dst_sz == 0)
        return;
    if (deneb_pending_job_file_display_value(value, dst, dst_sz) != 0)
        dst[0] = '\0';
}

void deneb_status_payload_resolve_filename_value(
    const char *file,
    int has_file,
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev,
    char *retained,
    size_t retained_sz,
    char *out,
    size_t out_sz)
{
    char pending_name[128];
    int has_pending_name;
    int hold;

    if (!curr || !prev || !out || out_sz == 0)
        return;

    out[0] = '\0';
    has_pending_name =
        deneb_pending_job_file_default_display_name(pending_name,
                                                    sizeof(pending_name)) == 0 &&
        pending_name[0] != '\0';

    if (has_file && file && file[0]) {
        if (deneb_print_file_is_transient(file)) {
            if (has_pending_name)
                set_display_or_empty(out, out_sz, pending_name);
            else if (retained && retained[0])
                set_display_or_empty(out, out_sz, retained);
            else if (prev->filename && prev->filename[0] &&
                     deneb_print_file_is_candidate(prev->filename))
                set_display_or_empty(out, out_sz, prev->filename);
        } else if (!deneb_print_file_is_candidate(file)) {
            if (retained && retained[0])
                set_display_or_empty(out, out_sz, retained);
            else if (prev->filename && prev->filename[0] &&
                     deneb_print_file_is_candidate(prev->filename))
                set_display_or_empty(out, out_sz, prev->filename);
        } else {
            set_display_or_empty(out, out_sz, file);
            if (retained && retained_sz > 0)
                set_display_or_empty(retained, retained_sz, file);
        }
    }

    hold = deneb_status_payload_should_hold_filename(curr, prev);
    if (!out[0] && hold && has_pending_name)
        set_display_or_empty(out, out_sz, pending_name);
    if (!out[0] && hold && retained && retained[0])
        set_display_or_empty(out, out_sz, retained);
    if (!out[0] && hold && prev->filename && prev->filename[0] &&
        deneb_print_file_is_candidate(prev->filename))
        set_display_or_empty(out, out_sz, prev->filename);

    if ((deneb_print_req_is_abort(curr->req) ||
         ((prev->is_printing || prev->is_paused) &&
          !curr->is_printing && !curr->is_paused)) &&
        retained && retained_sz > 0) {
        out[0] = '\0';
        retained[0] = '\0';
    }

    if (!out[0] && retained && retained[0] && !hold)
        set_display_or_empty(out, out_sz, retained);

    if (!hold && curr->time_total <= 0 && curr->time_left <= 0 &&
        curr->bed_target == 0.0f && curr->nozzle_target == 0.0f &&
        retained && retained_sz > 0) {
        retained[0] = '\0';
    }
}

void deneb_status_payload_resolve_filename(
    const deneb_status_payload_t *payload,
    const deneb_status_filename_context_t *curr,
    const deneb_status_filename_context_t *prev,
    char *retained,
    size_t retained_sz,
    char *out,
    size_t out_sz)
{
    if (!payload)
        return;

    deneb_status_payload_resolve_filename_value(
        payload->file, payload->has_file, curr, prev, retained, retained_sz,
        out, out_sz);
}
