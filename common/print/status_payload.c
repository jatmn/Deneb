/* SPDX-License-Identifier: MPL-2.0 */
#include "status_payload.h"

#include "json_field.h"

#include <stdio.h>
#include <string.h>

void deneb_status_payload_init(deneb_status_payload_t *payload)
{
    if (!payload)
        return;
    memset(payload, 0, sizeof(*payload));
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

    payload->observation.req = payload->req;
    payload->observation.file = payload->file;
    payload->observation.time_total = payload->time_total;
    payload->observation.time_left = payload->time_left;
    payload->observation.bed_target = payload->bed_temp_set;
    payload->observation.nozzle_target = payload->nozzle_temp_set;

    payload->is_paused = deneb_print_req_is_paused(payload->req);
    payload->is_printing = deneb_print_req_is_abort(payload->req) ?
        0 : deneb_print_observation_has_context(&payload->observation);
    payload->has_error = deneb_json_get_int(json, "received_faults", 0) != 0;
    return 0;
}
