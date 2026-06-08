/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_FRAME_LIGHT_H
#define DENEB_FRAME_LIGHT_H

#include <stddef.h>

#define DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS 100
#define DENEB_FRAME_LIGHT_LEGACY_UCI_KEY "ultimaker.option.framelight"
#define DENEB_FRAME_LIGHT_ENABLED_UCI_KEY "deneb.frame_light.enabled"
#define DENEB_FRAME_LIGHT_BRIGHTNESS_UCI_KEY "deneb.frame_light.brightness"

typedef struct {
    int enabled;
    int brightness;
} deneb_frame_light_state_t;

int deneb_frame_light_clamp_brightness(int value);
void deneb_frame_light_state_init(deneb_frame_light_state_t *state);
void deneb_frame_light_state_from_values(int legacy_brightness,
                                         int deneb_brightness,
                                         int deneb_enabled,
                                         deneb_frame_light_state_t *state);
int deneb_frame_light_read_saved_state(deneb_frame_light_state_t *state);
int deneb_frame_light_output_brightness(const deneb_frame_light_state_t *state);
int deneb_frame_light_format_save_command(
    const deneb_frame_light_state_t *state,
    char *out,
    size_t out_sz);

#endif
