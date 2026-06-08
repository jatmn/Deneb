/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINTER_STATUS_RESPONSE_H
#define DENEB_PRINTER_STATUS_RESPONSE_H

#include <stddef.h>

typedef struct {
    float nozzle_temp_cur;
    float nozzle_temp_set;
    float bed_temp_cur;
    float bed_temp_set;
    float pos_x;
    float pos_y;
    float pos_z;
    int connected;
    int is_printing;
    int is_paused;
    int has_error;
    int native_active;
    int native_stop_allowed;
    int has_native_active;
    int has_native_stop_allowed;
    int topcap_present;
    float progress;
    int time_total;
    int time_left;
    const char *filename;
    const char *status_label;
} deneb_printer_status_response_t;

void deneb_printer_status_response_init(
    deneb_printer_status_response_t *status);
int deneb_printer_status_response_format_status(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_root(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_bed(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_temperature(
    float current,
    float target,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_heads(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_head(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_position(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_extruders(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_extruder(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_hotend(
    const deneb_printer_status_response_t *status,
    int include_offset,
    char *out,
    size_t out_sz);
int deneb_printer_status_response_format_um_feeder(char *out, size_t out_sz);
int deneb_printer_status_response_format_um_material(char *out,
                                                     size_t out_sz);
int deneb_printer_status_response_format_um_led(char *out, size_t out_sz);
int deneb_printer_status_response_format_um_led_brightness(char *out,
                                                           size_t out_sz);
int deneb_printer_status_response_format_um_led_hue(char *out,
                                                    size_t out_sz);
int deneb_printer_status_response_format_um_led_saturation(char *out,
                                                           size_t out_sz);
int deneb_printer_status_response_format_um_ambient(char *out, size_t out_sz);
int deneb_printer_status_response_format_um_airmanager(
    const deneb_printer_status_response_t *status,
    char *out,
    size_t out_sz);

#endif
