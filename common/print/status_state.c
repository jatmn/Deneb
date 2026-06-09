/* SPDX-License-Identifier: MPL-2.0 */
#include "status_state.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0)
        return;

    snprintf(dst, dst_sz, "%s", src ? src : "");
}

void deneb_status_state_init(deneb_backend_status_state_t *state)
{
    if (state)
        memset(state, 0, sizeof(*state));
}

deneb_status_filename_context_t deneb_status_state_filename_context(
    const deneb_backend_status_state_t *state)
{
    return deneb_status_filename_context_from_fields(
        state ? state->current_req : NULL,
        state ? state->filename : NULL,
        state ? state->uuid : NULL,
        state ? state->time_total : 0,
        state ? state->time_left : 0,
        state ? state->bed_temp_set : 0.0f,
        state ? state->nozzle_temp_set : 0.0f,
        state ? state->is_printing : 0,
        state ? state->is_paused : 0);
}

int deneb_status_state_has_print_name(
    const deneb_backend_status_state_t *state,
    const char *display_name)
{
    return deneb_print_file_is_candidate(display_name) ||
           (state && deneb_print_file_is_candidate(state->filename));
}

int deneb_status_state_has_print_context(
    const deneb_backend_status_state_t *state)
{
    if (!state)
        return 0;

    return deneb_print_fields_have_active_context(
        state->current_req, state->filename, state->time_total,
        state->time_left, state->bed_temp_set, state->nozzle_temp_set,
        state->is_printing, state->is_paused,
        deneb_print_file_is_candidate(state->filename));
}

int deneb_status_state_has_abort_context(
    const deneb_backend_status_state_t *state,
    int stop_inflight)
{
    return (state && deneb_print_req_is_abort(state->current_req)) ||
           stop_inflight;
}

deneb_print_context_flags_t deneb_status_state_context_flags(
    const deneb_backend_status_state_t *state,
    int has_print_name)
{
    deneb_print_context_flags_t flags;

    deneb_print_context_flags_from_fields(
        &flags, state ? state->current_req : NULL,
        state && has_print_name ? state->filename : NULL,
        state ? state->time_total : 0,
        state ? state->time_left : 0,
        state ? state->bed_temp_set : 0.0f,
        state ? state->nozzle_temp_set : 0.0f,
        state ? state->is_printing : 0,
        state ? state->is_paused : 0,
        has_print_name);
    return flags;
}

int deneb_status_state_apply_json(deneb_backend_status_state_t *state,
                                  const deneb_backend_status_state_t *prev,
                                  const char *json,
                                  char *retained_filename,
                                  size_t retained_filename_sz,
                                  deneb_print_stop_guard_t *stop_guard,
                                  uint32_t now_ms)
{
    deneb_status_payload_t payload;
    deneb_backend_status_state_t empty_prev;
    deneb_status_filename_context_t curr_ctx;
    deneb_status_filename_context_t prev_ctx;

    if (!state || !json)
        return -1;

    if (deneb_status_payload_parse(json, &payload) != 0)
        return -1;

    if (!prev) {
        deneb_status_state_init(&empty_prev);
        prev = &empty_prev;
    }

    state->nozzle_temp_cur = payload.nozzle_temp_cur;
    state->nozzle_temp_set = payload.nozzle_temp_set;
    state->bed_temp_cur = payload.bed_temp_cur;
    state->bed_temp_set = payload.bed_temp_set;
    state->topcap_temp_cur = payload.topcap_temp_cur;
    state->topcap_present = payload.topcap_present != 0;
    state->pos_x = payload.pos_x;
    state->pos_y = payload.pos_y;
    state->pos_z = payload.pos_z;
    state->pos_e = payload.pos_e;
    state->time_total = payload.time_total;
    state->time_left = payload.time_left;
    state->progress = payload.progress;
    copy_text(state->source, sizeof(state->source), payload.source);
    copy_text(state->uuid, sizeof(state->uuid), payload.uuid);
    copy_text(state->current_req, sizeof(state->current_req), payload.req);
    copy_text(state->firmware, sizeof(state->firmware), payload.firmware);
    copy_text(state->machine_type, sizeof(state->machine_type),
              payload.machine_type);
    copy_text(state->error_key, sizeof(state->error_key), payload.error_key);
    copy_text(state->error_category, sizeof(state->error_category),
              payload.error_category);
    copy_text(state->error_detail, sizeof(state->error_detail),
              payload.error_detail);
    copy_text(state->flow_last_response, sizeof(state->flow_last_response),
              payload.flow_last_response);
    state->pcb_id = payload.pcb_id;
    state->pcb_id_valid = payload.pcb_id_valid != 0;
    state->flow_inflight = payload.flow_inflight;
    state->flow_sent = payload.flow_sent;
    state->flow_ack = payload.flow_ack;
    state->flow_resend = payload.flow_resend;
    state->flow_reject = payload.flow_reject;
    state->job_line_number = payload.job_line_number;
    state->is_printing = payload.is_printing != 0;
    state->is_paused = payload.is_paused != 0;
    state->native_active = payload.native_active != 0;
    state->native_stop_allowed = payload.native_stop_allowed != 0;
    state->has_native_active = payload.has_native_active != 0;
    state->has_native_stop_allowed = payload.has_native_stop_allowed != 0;

    curr_ctx = deneb_status_state_filename_context(state);
    prev_ctx = deneb_status_state_filename_context(prev);
    deneb_status_payload_resolve_filename(&payload, &curr_ctx, &prev_ctx,
                                          retained_filename,
                                          retained_filename_sz,
                                          state->filename,
                                          sizeof(state->filename));

    deneb_print_normalize_timing(state->is_printing, state->is_paused,
                                 &state->time_total, &state->time_left,
                                 &state->progress);
    state->has_error = payload.has_error != 0;

    curr_ctx = deneb_status_state_filename_context(state);
    if (stop_guard &&
        state->time_total <= 0 &&
        state->time_left <= 0 &&
        state->bed_temp_set <= 0.0f &&
        state->nozzle_temp_set <= 0.0f &&
        state->current_req[0] == '\0' &&
        !state->is_printing &&
        !state->is_paused) {
        deneb_print_stop_guard_clear(stop_guard);
    }

    state->connected = true;
    state->last_update_ms = now_ms;

    return 0;
}
