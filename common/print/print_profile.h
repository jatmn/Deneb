/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_COMMON_PRINT_PROFILE_H
#define DENEB_COMMON_PRINT_PROFILE_H

#include <stddef.h>

#define DENEB_PRINT_PROFILE_MACHINE_FAMILY "ultimaker2_plus_connect"
#define DENEB_PRINT_PROFILE_MACHINE_VARIANT "Ultimaker 2+ Connect"
#define DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"
#define DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_BRAND "Generic"
#define DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE "PLA"
#define DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_COLOR "#ffc924"
#define DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE "0.4"
#define DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_ID "0.4 mm"

typedef struct {
    const char *label;
    const char *guid;
} deneb_print_profile_material_choice_t;

typedef struct {
    const char *size;
    const char *label;
} deneb_print_profile_nozzle_choice_t;

size_t deneb_print_profile_material_choice_count(void);
const deneb_print_profile_material_choice_t *
deneb_print_profile_material_choice(size_t index);
const char *deneb_print_profile_material_label_from_guid(const char *guid);
int deneb_print_profile_format_set_material_command(const char *guid,
                                                    char *out,
                                                    size_t out_sz);
size_t deneb_print_profile_nozzle_choice_count(void);
const deneb_print_profile_nozzle_choice_t *
deneb_print_profile_nozzle_choice(size_t index);
const char *deneb_print_profile_nozzle_label_from_size(const char *size);
int deneb_print_profile_format_set_nozzle_command(const char *size,
                                                  char *out,
                                                  size_t out_sz);
void deneb_print_profile_read_loaded_material_guid(char *out, size_t out_sz);
void deneb_print_profile_read_loaded_nozzle_size(char *out, size_t out_sz);
int deneb_print_profile_normalize_nozzle_size(const char *value,
                                              char *out,
                                              size_t out_sz);
void deneb_print_profile_read_loaded_nozzle_id(char *out, size_t out_sz);
void deneb_print_profile_normalize_nozzle_id(const char *value,
                                             char *out,
                                             size_t out_sz);
void deneb_print_profile_material_name_from_guid(const char *guid,
                                                 char *out,
                                                 size_t out_sz);

#endif
