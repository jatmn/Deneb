/* SPDX-License-Identifier: MPL-2.0 */
#include "status_payload.h"

#include "json_field.h"
#include "pending_job_file.h"

#include <stdio.h>
#include <stdlib.h>
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

static int string_equals_ci(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if (!a || !b)
        return 0;

    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }

    return *a == '\0' && *b == '\0';
}

static int json_value_has_faults(const char *value)
{
    const char *p;

    if (!value || !*value)
        return 0;

    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "none") == 0 || strcmp(value, DENEB_PRINT_NONE_VALUE) == 0)
        return 0;

    if (value[0] != '[')
        return 1;

    p = value + 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return *p != ']';
}

typedef enum {
    STATUS_TEXT,
    STATUS_FLOAT,
    STATUS_INT,
    STATUS_TRUTH,
    STATUS_NAME,
    STATUS_MANUAL_ID,
    STATUS_MANUAL_STATE,
    STATUS_ACTIVE,
    STATUS_STOP_ALLOWED,
    STATUS_ERROR_KEY,
    STATUS_FAULTS,
    STATUS_STATE
} status_value_kind_t;

typedef struct {
    const char *name;
    unsigned short offset;
    unsigned short value_size;
    unsigned char name_size;
    unsigned char kind;
} status_field_t;

#define FIELD(name, kind, member) \
    {name, offsetof(deneb_status_payload_t, member), \
     sizeof(((deneb_status_payload_t *)0)->member), sizeof(name) - 1, STATUS_##kind}
#define SPECIAL(name, kind) {name, 0, 0, sizeof(name) - 1, STATUS_##kind}

/* In order of key length for the bounded lookup. */
static const status_field_t status_fields[] = {
    FIELD("E", FLOAT, pos_e),
    FIELD("X", FLOAT, pos_x),
    FIELD("Y", FLOAT, pos_y),
    FIELD("Z", FLOAT, pos_z),
    FIELD("req", TEXT, req),
    FIELD("Ttot", INT, time_total),
    FIELD("file", TEXT, file),
    SPECIAL("name", NAME),
    FIELD("uuid", TEXT, uuid),
    FIELD("Tleft", INT, time_left),
    FIELD("pcbId", INT, pcb_id),
    FIELD("source", TEXT, source),
    FIELD("bedTcur", FLOAT, bed_temp_cur),
    FIELD("bedTset", FLOAT, bed_temp_set),
    FIELD("flowAck", INT, flow_ack),
    FIELD("firmware", TEXT, firmware),
    FIELD("flowSent", INT, flow_sent),
    FIELD("headTcur", FLOAT, nozzle_temp_cur),
    FIELD("headTset", FLOAT, nozzle_temp_set),
    SPECIAL("manualId", MANUAL_ID),
    SPECIAL("denebState", STATE),
    FIELD("flowReject", INT, flow_reject),
    FIELD("flowResend", INT, flow_resend),
    FIELD("pcbIdValid", TRUTH, pcb_id_valid),
    SPECIAL("denebActive", ACTIVE),
    FIELD("machineType", TEXT, machine_type),
    SPECIAL("manualState", MANUAL_STATE),
    FIELD("cloud_job_id", TEXT, cloud_job_id),
    FIELD("flowInflight", INT, flow_inflight),
    FIELD("denebErrorKey", ERROR_KEY, error_key),
    FIELD("jobLineNumber", INT, job_line_number),
    SPECIAL("received_faults", FAULTS),
    FIELD("topcapIsPresent", TRUTH, topcap_present),
    FIELD("denebErrorDetail", TEXT, error_detail),
    SPECIAL("denebStopAllowed", STOP_ALLOWED),
    FIELD("flowLastResponse", TEXT, flow_last_response),
    FIELD("topcapTemperature", FLOAT, topcap_temp_cur),
    FIELD("denebErrorCategory", TEXT, error_category),
};

#undef FIELD
#undef SPECIAL

enum { STATUS_FIELD_COUNT = sizeof(status_fields) / sizeof(status_fields[0]) };

static int find_status_field(const char *name, size_t size)
{
    if (!size || size > status_fields[STATUS_FIELD_COUNT - 1].name_size)
        return -1;

    unsigned lower = 0;
    unsigned upper = STATUS_FIELD_COUNT;
    while (lower < upper) {
        unsigned middle = lower + (upper - lower) / 2;
        if (status_fields[middle].name_size < size)
            lower = middle + 1;
        else
            upper = middle;
    }

    for (unsigned index = lower;
         index < STATUS_FIELD_COUNT && status_fields[index].name_size == size;
         index++) {
        if (memcmp(name, status_fields[index].name, size) == 0)
            return (int)index;
    }
    return -1;
}

static void parse_status_field(const status_field_t *field, const char *value,
                               size_t size, deneb_status_payload_t *payload,
                               char *name)
{
    char decoded[64];
    char *target = (char *)payload + field->offset;

    if (field->kind == STATUS_NAME) {
        if (deneb_json_copy_value_span(value, size, name, sizeof(payload->file)) != 0)
            name[0] = '\0';
        return;
    }
    if (field->kind == STATUS_TEXT || field->kind == STATUS_ERROR_KEY) {
        if (deneb_json_copy_value_span(value, size, target, field->value_size) != 0)
            target[0] = '\0';
        if (field->kind == STATUS_TEXT)
            return;
        /* The error flag has always accepted a wider key than the display. */
    }

    size_t decoded_size = sizeof(decoded);
    if (field->kind == STATUS_TRUTH || field->kind == STATUS_ACTIVE ||
        field->kind == STATUS_STOP_ALLOWED || field->kind == STATUS_MANUAL_ID)
        decoded_size = 32;
    if (deneb_json_copy_value_span(value, size, decoded, decoded_size) != 0)
        return;

    switch (field->kind) {
    case STATUS_FLOAT: *(float *)target = strtof(decoded, NULL); break;
    case STATUS_INT: *(int *)target = (int)strtol(decoded, NULL, 10); break;
    case STATUS_TRUTH: *(int *)target = deneb_json_value_is_truthy(decoded); break;
    case STATUS_MANUAL_ID: payload->manual_id = deneb_manual_id_parse(decoded); break;
    case STATUS_MANUAL_STATE:
        payload->manual_state = (deneb_manual_state_t)strtol(decoded, NULL, 10);
        break;
    case STATUS_ACTIVE:
        payload->has_native_active = 1;
        payload->native_active = deneb_json_value_is_truthy(decoded);
        break;
    case STATUS_STOP_ALLOWED:
        payload->has_native_stop_allowed = 1;
        payload->native_stop_allowed = deneb_json_value_is_truthy(decoded);
        break;
    case STATUS_FAULTS: payload->has_error |= json_value_has_faults(decoded); break;
    case STATUS_STATE: payload->has_error |= string_equals_ci(decoded, "error"); break;
    case STATUS_ERROR_KEY:
        payload->has_error |= decoded[0] && !string_equals_ci(decoded, DENEB_PRINT_NONE_VALUE);
        break;
    }
}

int deneb_status_payload_parse_span(const char *json, size_t size,
                                    deneb_status_payload_t *payload)
{
    if (!json || !payload)
        return -1;

    char name[sizeof(payload->file)] = "";
    unsigned char seen[STATUS_FIELD_COUNT] = {0};
    deneb_status_payload_init(payload);
    const char *end = json + size;
    const char *previous_quote = NULL;
    /* Match the first raw quoted key, including the legacy duplicate behavior. */
    for (const char *p = json; p != end && *p; p++) {
        if (*p != '"')
            continue;
        if (previous_quote) {
            int field = find_status_field(previous_quote + 1,
                                          (size_t)(p - previous_quote - 1));
            if (field >= 0 && !seen[field]) {
                seen[field] = 1;
                parse_status_field(&status_fields[field], p + 1,
                                   (size_t)(end - p - 1), payload, name);
            }
        }
        previous_quote = p;
    }

    payload->progress = deneb_print_progress_percent(payload->time_total,
                                                     payload->time_left);
    if ((!payload->file[0] || strcmp(payload->file, DENEB_PRINT_NONE_VALUE) == 0) &&
        name[0] && strcmp(name, DENEB_PRINT_NONE_VALUE) != 0) {
        snprintf(payload->file, sizeof(payload->file), "%s", name);
    }
    payload->has_file = payload->file[0] &&
        strcmp(payload->file, DENEB_PRINT_NONE_VALUE) != 0;

    deneb_print_observation_init(&payload->observation, payload->req,
                                 payload->file, payload->time_total,
                                 payload->time_left, payload->bed_temp_set,
                                 payload->nozzle_temp_set);

    payload->is_paused = deneb_print_req_is_paused(payload->req);
    payload->is_printing = deneb_print_req_is_abort(payload->req) ?
        0 : deneb_print_observation_has_context(&payload->observation);
    if (payload->has_native_active)
        payload->is_printing = payload->native_active;
    return 0;
}

int deneb_status_payload_parse(const char *json, deneb_status_payload_t *payload)
{
    return json ? deneb_status_payload_parse_span(json, strlen(json), payload) : -1;
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

static int resolve_pending_display_name(char *out, size_t out_sz)
{
    char pending_name[128];

    if (deneb_pending_job_file_default_display_name(
            pending_name, sizeof(pending_name)) != 0 || !pending_name[0])
        return 0;
    set_display_or_empty(out, out_sz, pending_name);
    return 1;
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
    int transient;
    int hold;

    if (!curr || !prev || !out || out_sz == 0)
        return;

    out[0] = '\0';
    transient = has_file && deneb_print_file_is_transient(file);

    if (has_file && file && file[0]) {
        if (!transient && deneb_print_file_is_candidate(file)) {
            set_display_or_empty(out, out_sz, file);
            if (retained && retained_sz > 0)
                set_display_or_empty(retained, retained_sz, file);
        } else if (!transient || !resolve_pending_display_name(out, out_sz)) {
            if (retained && retained[0])
                set_display_or_empty(out, out_sz, retained);
            else if (prev->filename && prev->filename[0] &&
                     deneb_print_file_is_candidate(prev->filename))
                set_display_or_empty(out, out_sz, prev->filename);
        }
    }

    hold = deneb_status_payload_should_hold_filename(curr, prev);
    if (!out[0] && hold && !transient)
        resolve_pending_display_name(out, out_sz);
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
